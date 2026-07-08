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

    void PluginFactory::Impl::preloadSharedLibraries(const fs::path &sharedDir) const {
        if (!fs::is_directory(sharedDir))
            return;
        for (const auto &entry : fs::directory_iterator(sharedDir)) {
            const auto &p = entry.path();
            if (!stdc::SharedLibrary::isLibrary(p))
                continue;
            stdc::SharedLibrary lib;
            if (lib.open(p)) {
                // pre-loaded, will be kept in process memory
            }
        }
    }

    void PluginFactory::Impl::scanPlugins(const char *iid) const {
        auto &plugins = allPlugins[iid];

        // Add runtime plugins
        for (const auto &plugin : runtimePlugins) {
            if (strcmp(iid, plugin->iid()) == 0) {
                std::ignore = plugins.insert(std::make_pair(plugin->key(), plugin));
            }
        }

        // Pre-load shared libraries from _shared/ dir (once)
        if (!sharedLoaded) {
            auto sharedDirIt = pluginDirs.find(iid);
            if (sharedDirIt != pluginDirs.end() && !sharedDirIt->second.empty()) {
                auto sharedDir = sharedDirIt->second.front().parent_path().parent_path() / "_shared";
                preloadSharedLibraries(sharedDir);
                sharedLoaded = true;
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
        if (!fs::is_directory(path)) {
            return;
        }

        std::unique_lock<std::shared_mutex> lock(impl.plugins_mtx);
        const fs::path canonicalPath = fs::canonical(path);

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

        if (!paths.empty()) {
            llvm::SmallVector<fs::path> dirs;
            for (const auto &path : paths) {
                if (!fs::is_directory(path)) continue;
                fs::path canonicalPath = fs::canonical(path);
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
