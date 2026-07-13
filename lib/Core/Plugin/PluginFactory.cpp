#include "PluginFactory.h"
#include "PluginFactory_p.h"

#include <utility>
#include <cstring>
#include <fstream>
#include <mutex>

#include <nlohmann/json.hpp>

#include <stdcorelib/path.h>
#include <stdcorelib/pimpl.h>
#include <stdcorelib/str.h>
#include <stdcorelib/3rdparty/llvm/smallvector.h>

#include <synthrt/Core/Support/Logging.h>

namespace fs = std::filesystem;

namespace srt::core {

    using StaticPluginMap = std::map<std::string, llvm::SmallVector<StaticPlugin, 10>>;

    static srt::LogCategory PluginLog("plugin.factory");

    static StaticPluginMap &getStaticPluginMap() {
        static StaticPluginMap staticPluginMap;
        return staticPluginMap;
    }

    void StaticPlugin::registerStaticPlugin(const char *pluginSet, StaticPlugin plugin) {
        auto &plugins = getStaticPluginMap()[pluginSet];

        static const auto comparator = [=](const StaticPlugin &p1, const StaticPlugin &p2) {
            using Less = std::less<decltype(plugin.instance)>;
            return Less{}(p1.instance, p2.instance);
        };
        auto pos = std::lower_bound(plugins.begin(), plugins.end(), plugin, comparator);
        if (pos == plugins.end() || pos->instance != plugin.instance)
            plugins.insert(pos, plugin);
    }

    PluginFactory::Impl::Impl(PluginFactory *decl) : _decl(decl) {
    }

    PluginFactory::Impl::~Impl() {
        for (const auto &item : std::as_const(libraryInstances)) {
            delete item.second;
        }
    }

    bool PluginFactory::Impl::preloadSharedLibraries(const fs::path &sharedDir) const {
        std::error_code ec;
        if (!fs::is_directory(sharedDir, ec))
            return false;

        bool allLoaded = true;
        for (fs::directory_iterator it(sharedDir, ec), end; !ec && it != end; it.increment(ec)) {
            const auto &entry = *it;
            const auto &p = entry.path();
            if (!stdc::SharedLibrary::isLibrary(p))
                continue;

            const auto &key = p.native();
            if (preloadedLibraries.count(key))
                continue;

            stdc::SharedLibrary lib;
            if (lib.open(p)) {
                preloadedLibraries.emplace(key,
                                           std::make_unique<stdc::SharedLibrary>(std::move(lib)));
            } else {
                allLoaded = false;
                PluginLog.srtWarning("preloadSharedLibraries: failed to load shared library %1",
                                     stdc::path::to_utf8(p));
            }
        }
        return !ec && allLoaded;
    }

    fs::path PluginFactory::Impl::sharedLibraryPath(const fs::path &categoryDir) {
        std::error_code ec;
        auto normalizedPath = fs::weakly_canonical(categoryDir, ec);
        if (ec) {
            ec.clear();
            normalizedPath = fs::absolute(categoryDir, ec);
            if (ec)
                normalizedPath = categoryDir;
            normalizedPath = normalizedPath.lexically_normal();
        }
        return normalizedPath.parent_path().parent_path() / "_shared";
    }

    void PluginFactory::Impl::scanPlugins(const char *iid) const {
        auto &plugins = allPlugins[iid];

        // Add runtime plugins
        for (const auto &plugin : runtimePlugins) {
            if (strcmp(iid, plugin->iid()) == 0) {
                std::ignore = plugins.insert(std::make_pair(plugin->key(), plugin));
            }
        }

        // Pre-load each global shared directory once, on the first scan that references it.
        bool sharedPreloadPending = false;
        if (auto sharedDirIt = sharedDirs.find(iid); sharedDirIt != sharedDirs.end()) {
            for (const auto &sharedDir : sharedDirIt->second) {
                const auto key = sharedDir.native();
                if (loadedSharedDirs.count(key))
                    continue;
                if (preloadSharedLibraries(sharedDir))
                    loadedSharedDirs.insert(key);
                else
                    sharedPreloadPending = true;
            }
        }

        auto dirIt = pluginDirs.find(iid);
        if (dirIt != pluginDirs.end()) {
            for (const auto &pluginDir : dirIt->second) {
                fs::path descPath = pluginDir / "plugin.json";
                if (!fs::exists(descPath)) {
                    PluginLog.srtWarning("scanPlugins: plugin.json not found in %1, skipping",
                                         stdc::path::to_utf8(pluginDir));
                    continue;
                }

                std::string pluginDirStr = stdc::path::to_utf8(pluginDir);
                if (scannedPluginDirs.count(pluginDirStr) > 0)
                    continue;

                // Parse plugin.json
                struct PluginDesc { std::string target; bool valid = false; };
                PluginDesc desc;
                try {
                    std::ifstream ifs(descPath);
                    if (!ifs.is_open()) {
                        PluginLog.srtWarning("scanPlugins: failed to open plugin.json in %1, skipping",
                                             stdc::path::to_utf8(pluginDir));
                        continue;
                    }
                    const std::string jsonStr((std::istreambuf_iterator(ifs)), std::istreambuf_iterator<char>());

                    std::string jsonErr;
                    auto jsonVal = nlohmann::json::parse(jsonStr, nullptr, false);
                    if (jsonVal.is_discarded()) {
                        PluginLog.srtWarning("scanPlugins: invalid JSON in plugin.json in %1, skipping",
                                             stdc::path::to_utf8(pluginDir));
                        continue;
                    }
                    auto &obj = jsonVal;
                    auto it = obj.find("target");
                    if (it == obj.end() || !it->is_string()) {
                        PluginLog.srtWarning("scanPlugins: plugin.json in %1 missing string 'target' field, skipping",
                                             stdc::path::to_utf8(pluginDir));
                        continue;
                    }
                    desc.target = it->get<std::string>();
                    desc.valid = true;
                } catch (const std::exception &e) {
                    PluginLog.srtWarning("scanPlugins: exception while parsing plugin.json in %1: %2, skipping",
                                         stdc::path::to_utf8(pluginDir), e.what());
                    continue;
                }
                if (!desc.valid)
                    continue;

                fs::path dllPath = pluginDir / desc.target;
                if (!fs::exists(dllPath) || !stdc::SharedLibrary::isLibrary(dllPath)) {
                    PluginLog.srtWarning("scanPlugins: shared library not found or invalid: %1 (target=%2), skipping",
                                         stdc::path::to_utf8(dllPath), desc.target);
                    continue;
                }

                if (libraryInstances.count(dllPath))
                    continue;

                stdc::SharedLibrary so;
                stdc::SharedLibrary::setLibraryPath(pluginDir);
                if (!so.open(dllPath)) {
                    PluginLog.srtWarning("scanPlugins: failed to load shared library %1, skipping",
                                         stdc::path::to_utf8(dllPath));
                    continue;
                }

                using PluginGetter = Plugin *(*)();
                auto getter = reinterpret_cast<PluginGetter>(so.resolve("srt_plugin_instance"));
                if (!getter) {
                    PluginLog.srtWarning("scanPlugins: symbol 'srt_plugin_instance' not found in %1, skipping",
                                         stdc::path::to_utf8(dllPath));
                    continue;
                }

                auto plugin = getter();
                if (!plugin) {
                    PluginLog.srtWarning("scanPlugins: srt_plugin_instance returned null in %1, skipping",
                                         stdc::path::to_utf8(dllPath));
                    continue;
                }
                if (strcmp(iid, plugin->iid()) != 0) {
                    PluginLog.srtWarning("scanPlugins: plugin iid mismatch in %1: expected %2, got %3, skipping",
                                         stdc::path::to_utf8(dllPath), iid, plugin->iid());
                    continue;
                }
                if (!plugins.insert(std::make_pair(plugin->key(), plugin)).second) {
                    PluginLog.srtWarning("scanPlugins: duplicate plugin key '%1' in %2, skipping",
                                         plugin->key(), stdc::path::to_utf8(dllPath));
                    continue;
                }

                libraryInstances[dllPath] = new stdc::SharedLibrary(std::move(so));
                scannedPluginDirs.insert(pluginDirStr);
            }
        }

        if (plugins.empty())
            allPlugins.erase(iid);
        if (!sharedPreloadPending)
            pluginsDirty.erase(iid);
    }

    PluginFactory::PluginFactory() : _impl(new Impl(this)) {
    }

    PluginFactory::~PluginFactory() = default;

    std::vector<std::string> PluginFactory::staticPluginSets() {
        auto &map = getStaticPluginMap();
        std::vector<std::string> pluginSets;
        pluginSets.reserve(map.size());
        for (const auto &item : map) {
            pluginSets.push_back(item.first);
        }
        return pluginSets;
    }

    std::vector<StaticPlugin> PluginFactory::staticPlugins(const char *pluginSet) {
        auto &map = getStaticPluginMap();
        auto it = map.find(pluginSet);
        if (it == map.end()) {
            return {};
        }
        return {it->second.begin(), it->second.end()};
    }

    std::vector<Plugin *> PluginFactory::staticInstances(const char *pluginSet) {
        auto &map = getStaticPluginMap();
        std::vector<Plugin *> instances;
        auto it = map.find(pluginSet);
        if (it == map.end()) {
            return {};
        }
        const auto &plugins = it->second;
        instances.reserve(plugins.size());
        for (StaticPlugin plugin : plugins)
            instances.push_back(plugin.instance());
        return instances;
    }

    void PluginFactory::addRuntimePlugin(Plugin *plugin) {
        __stdc_impl_t;
        std::unique_lock<std::shared_mutex> lock(impl.plugins_mtx);
        impl.runtimePlugins.emplace(plugin);
        impl.pluginsDirty.insert(plugin->iid());
    }

    std::vector<Plugin *> PluginFactory::runtimePlugins() const {
        __stdc_impl_t;
        std::shared_lock<std::shared_mutex> lock(impl.plugins_mtx);
        return {impl.runtimePlugins.begin(), impl.runtimePlugins.end()};
    }

    void PluginFactory::addPluginPath(const char *iid, const std::filesystem::path &path) {
        __stdc_impl_t;

        std::unique_lock<std::shared_mutex> lock(impl.plugins_mtx);
        auto &sharedDirs = impl.sharedDirs[iid];
        const auto sharedDir = Impl::sharedLibraryPath(path);
        if (std::find(sharedDirs.begin(), sharedDirs.end(), sharedDir) == sharedDirs.end())
            sharedDirs.push_back(sharedDir);

        std::error_code ec;
        if (!fs::is_directory(path, ec)) {
            impl.pluginsDirty.insert(iid);
            return;
        }

        const fs::path canonicalPath = fs::canonical(path, ec);
        if (ec) {
            impl.pluginsDirty.insert(iid);
            return;
        }

        // Scan subdirectories for plugin.json descriptors
        for (const auto &entry : fs::directory_iterator(canonicalPath)) {
            if (!entry.is_directory()) {
                continue;
            }
            const auto &pluginDir = fs::canonical(entry.path());
            if (fs::path descPath = pluginDir / "plugin.json"; fs::exists(descPath)) {
                impl.pluginDirs[iid].push_back(pluginDir);
            }
        }

        impl.pluginsDirty.insert(iid);
    }

    void PluginFactory::setPluginPaths(const char *iid,
                                       stdc::array_view<std::filesystem::path> paths) {
        __stdc_impl_t;
        std::unique_lock<std::shared_mutex> lock(impl.plugins_mtx);

        impl.pluginDirs.erase(iid);
        impl.sharedDirs.erase(iid);

        if (!paths.empty()) {
            llvm::SmallVector<fs::path> dirs;
            llvm::SmallVector<fs::path> sharedDirs;
            for (const auto &path : paths) {
                const auto sharedDir = Impl::sharedLibraryPath(path);
                if (std::find(sharedDirs.begin(), sharedDirs.end(), sharedDir) == sharedDirs.end())
                    sharedDirs.push_back(sharedDir);
                std::error_code ec;
                if (!fs::is_directory(path, ec)) continue;
                fs::path canonicalPath = fs::canonical(path, ec);
                if (ec) continue;
                for (const auto &entry : fs::directory_iterator(canonicalPath)) {
                    if (!entry.is_directory()) continue;
                    const auto &pluginDir = fs::canonical(entry.path());
                    if (fs::path descPath = pluginDir / "plugin.json"; fs::exists(descPath)) {
                        dirs.push_back(pluginDir);
                    }
                }
            }
            if (!dirs.empty()) {
                impl.pluginDirs[iid] = dirs;
            }
            if (!sharedDirs.empty()) {
                impl.sharedDirs[iid] = std::move(sharedDirs);
            }
        }

        impl.pluginsDirty.insert(iid);
    }

    std::vector<std::filesystem::path> PluginFactory::pluginPaths(const char *iid) const {
        __stdc_impl_t;

        std::shared_lock<std::shared_mutex> lock(impl.plugins_mtx);
        auto it = impl.pluginDirs.find(iid);
        if (it == impl.pluginDirs.end()) {
            return {};
        }
        return {it->second.begin(), it->second.end()};
    }

    Plugin *PluginFactory::plugin(const char *iid, const char *key) const {
        __stdc_impl_t;

        std::unique_lock<std::shared_mutex> lock(impl.plugins_mtx);
        if (impl.pluginsDirty.count(iid)) {
            impl.scanPlugins(iid);
        }

        auto it = impl.allPlugins.find(iid);
        if (it == impl.allPlugins.end()) {
            return nullptr;
        }

        const auto &pluginsMap = it->second;
        auto it2 = pluginsMap.find(key);
        if (it2 == pluginsMap.end()) {
            return nullptr;
        }
        return it2->second;
    }

    /*!
        \internal
    */
    PluginFactory::PluginFactory(Impl &impl) : _impl(&impl) {
    }

}
