#include "PluginFactory.h"

#include <stdcorelib/path.h>
#include <stdcorelib/pimpl.h>
#include <stdcorelib/str.h>
#include <synthrt/Core/Support/Logging.h>

#include <cstring>
#include <fstream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <utility>

#include "PluginFactory_p.h"

namespace fs = std::filesystem;

namespace srt::core {

    using StaticPluginMap = std::map<std::string, std::vector<StaticPlugin>>;

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

    PluginFactory::Impl::Impl(PluginFactory *q) : m_q(q) {
    }

    // CODING-04: destructor is defaulted — m_libraryInstances now holds
    // unique_ptr<SharedLibrary>, so cleanup is automatic.
    PluginFactory::Impl::~Impl() = default;

    bool PluginFactory::Impl::preloadSharedLibraries(const fs::path &sharedDir) const {
        std::error_code ec;
        if (!fs::is_directory(sharedDir, ec))
            return false;

        bool allLoaded = true;
        for (fs::directory_iterator it(sharedDir, ec), end; !ec && it != end; it.increment(ec)) {
            const auto &entry = *it;
            const auto &p     = entry.path();
            if (!stdc::SharedLibrary::isLibrary(p))
                continue;

            const auto &key = p.native();
            if (m_preloadedLibraries.count(key))
                continue;

            stdc::SharedLibrary lib;
            if (lib.open(p)) {
                m_preloadedLibraries.emplace(key, std::make_unique<stdc::SharedLibrary>(std::move(lib)));
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
        auto            normalizedPath = fs::weakly_canonical(categoryDir, ec);
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
        auto &plugins = m_allPlugins[iid];

        // Add runtime plugins
        for (const auto &plugin : m_runtimePlugins) {
            if (strcmp(iid, plugin->iid()) == 0) {
                std::ignore = plugins.insert(std::make_pair(plugin->key(), plugin));
            }
        }

        // Pre-load each global shared directory once, on the first scan that references it.
        bool sharedPreloadPending = false;
        if (auto sharedDirIt = m_sharedDirs.find(iid); sharedDirIt != m_sharedDirs.end()) {
            for (const auto &sharedDir : sharedDirIt->second) {
                const auto key = sharedDir.native();
                if (m_loadedSharedDirs.count(key))
                    continue;
                if (preloadSharedLibraries(sharedDir))
                    m_loadedSharedDirs.insert(key);
                else
                    sharedPreloadPending = true;
            }
        }

        auto dirIt = m_pluginDirs.find(iid);
        if (dirIt != m_pluginDirs.end()) {
            for (const auto &pluginDir : dirIt->second) {
                fs::path descPath = pluginDir / "plugin.json";
                if (!fs::exists(descPath)) {
                    PluginLog.srtWarning("scanPlugins: plugin.json not found in %1, skipping",
                                         stdc::path::to_utf8(pluginDir));
                    continue;
                }

                std::string pluginDirStr = stdc::path::to_utf8(pluginDir);
                if (m_scannedPluginDirs.count(pluginDirStr) > 0)
                    continue;

                // Parse plugin.json
                struct PluginDesc {
                    std::string target;
                    bool        valid = false;
                };
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
                    auto        jsonVal = nlohmann::json::parse(jsonStr, nullptr, false);
                    if (jsonVal.is_discarded()) {
                        PluginLog.srtWarning("scanPlugins: invalid JSON in plugin.json in %1, skipping",
                                             stdc::path::to_utf8(pluginDir));
                        continue;
                    }
                    auto &obj = jsonVal;
                    auto  it  = obj.find("target");
                    if (it == obj.end() || !it->is_string()) {
                        PluginLog.srtWarning("scanPlugins: plugin.json in %1 missing string 'target' field, skipping",
                                             stdc::path::to_utf8(pluginDir));
                        continue;
                    }
                    desc.target = it->get<std::string>();
                    desc.valid  = true;
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

                if (m_libraryInstances.count(dllPath))
                    continue;

                stdc::SharedLibrary so;
                stdc::SharedLibrary::setLibraryPath(pluginDir);
                if (!so.open(dllPath)) {
                    PluginLog.srtWarning("scanPlugins: failed to load shared library %1, skipping",
                                         stdc::path::to_utf8(dllPath));
                    continue;
                }

                using PluginGetter = Plugin *(*)();
                auto getter        = reinterpret_cast<PluginGetter>(so.resolve("srt_plugin_instance"));
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
                    // A collection directory may legitimately host plugins of
                    // different iids (e.g. diffsinger/inferenceinterpreters/
                    // holds acoustic/duration/pitch/variance/vocoder side by
                    // side). Skipping non-matching iids is normal discovery
                    // behavior, not a warning-worthy condition. Demoted from
                    // Warning to Trace to avoid noisy logs when a host scans
                    // a shared plugin root for multiple iids.
                    PluginLog.srtTrace("scanPlugins: plugin iid mismatch in %1: expected %2, got %3, skipping",
                                       stdc::path::to_utf8(dllPath), iid, plugin->iid());
                    continue;
                }
                if (!plugins.insert(std::make_pair(plugin->key(), plugin)).second) {
                    PluginLog.srtWarning("scanPlugins: duplicate plugin key '%1' in %2, skipping", plugin->key(),
                                         stdc::path::to_utf8(dllPath));
                    continue;
                }

                // CODING-04: own the SharedLibrary via unique_ptr (no bare new).
                m_libraryInstances.emplace(dllPath, std::make_unique<stdc::SharedLibrary>(std::move(so)));
                m_scannedPluginDirs.insert(pluginDirStr);
            }
        }

        if (plugins.empty())
            m_allPlugins.erase(iid);
        if (!sharedPreloadPending)
            m_pluginsDirty.erase(iid);
    }

    PluginFactory::PluginFactory() : _impl(new Impl(this)) {
    }

    PluginFactory::~PluginFactory() = default;

    std::vector<std::string> PluginFactory::staticPluginSets() {
        auto                    &map = getStaticPluginMap();
        std::vector<std::string> pluginSets;
        pluginSets.reserve(map.size());
        for (const auto &item : map) {
            pluginSets.push_back(item.first);
        }
        return pluginSets;
    }

    std::vector<StaticPlugin> PluginFactory::staticPlugins(const char *pluginSet) {
        auto &map = getStaticPluginMap();
        auto  it  = map.find(pluginSet);
        if (it == map.end()) {
            return {};
        }
        return {it->second.begin(), it->second.end()};
    }

    std::vector<Plugin *> PluginFactory::staticInstances(const char *pluginSet) {
        auto                 &map = getStaticPluginMap();
        std::vector<Plugin *> instances;
        auto                  it = map.find(pluginSet);
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
        stdc_impl_t;
        std::unique_lock<std::shared_mutex> lock(impl.m_plugins_mtx);
        impl.m_runtimePlugins.emplace(plugin);
        impl.m_pluginsDirty.insert(plugin->iid());
    }

    std::vector<Plugin *> PluginFactory::runtimePlugins() const {
        stdc_impl_t;
        std::shared_lock<std::shared_mutex> lock(impl.m_plugins_mtx);
        return {impl.m_runtimePlugins.begin(), impl.m_runtimePlugins.end()};
    }

    void PluginFactory::addPluginPath(const char *iid, const std::filesystem::path &path) {
        stdc_impl_t;

        std::unique_lock<std::shared_mutex> lock(impl.m_plugins_mtx);
        auto                               &sharedDirs = impl.m_sharedDirs[iid];
        const auto                          sharedDir  = Impl::sharedLibraryPath(path);
        if (std::find(sharedDirs.begin(), sharedDirs.end(), sharedDir) == sharedDirs.end())
            sharedDirs.push_back(sharedDir);

        std::error_code ec;
        if (!fs::is_directory(path, ec)) {
            impl.m_pluginsDirty.insert(iid);
            return;
        }

        const fs::path canonicalPath = fs::canonical(path, ec);
        if (ec) {
            impl.m_pluginsDirty.insert(iid);
            return;
        }

        // Scan subdirectories for plugin.json descriptors
        try {
            for (const auto &entry : fs::directory_iterator(canonicalPath)) {
                if (!entry.is_directory()) {
                    continue;
                }
                const auto &pluginDir = fs::canonical(entry.path());
                if (fs::path descPath = pluginDir / "plugin.json"; fs::exists(descPath)) {
                    impl.m_pluginDirs[iid].push_back(pluginDir);
                }
            }
        } catch (const std::exception &e) {
            PluginLog.srtWarning("addPluginPath: failed to scan plugin path %1: %2", stdc::path::to_utf8(canonicalPath),
                                 e.what());
            impl.m_pluginsDirty.insert(iid);
            return;
        }

        impl.m_pluginsDirty.insert(iid);
    }

    void PluginFactory::setPluginPaths(const char *iid, stdc::array_view<std::filesystem::path> paths) {
        stdc_impl_t;
        std::unique_lock<std::shared_mutex> lock(impl.m_plugins_mtx);

        impl.m_pluginDirs.erase(iid);
        impl.m_sharedDirs.erase(iid);

        if (!paths.empty()) {
            std::vector<fs::path> dirs;
            std::vector<fs::path> sharedDirs;
            for (const auto &path : paths) {
                const auto sharedDir = Impl::sharedLibraryPath(path);
                if (std::find(sharedDirs.begin(), sharedDirs.end(), sharedDir) == sharedDirs.end())
                    sharedDirs.push_back(sharedDir);
                std::error_code ec;
                if (!fs::is_directory(path, ec))
                    continue;
                fs::path canonicalPath = fs::canonical(path, ec);
                if (ec)
                    continue;
                try {
                    for (const auto &entry : fs::directory_iterator(canonicalPath)) {
                        if (!entry.is_directory())
                            continue;
                        const auto &pluginDir = fs::canonical(entry.path());
                        if (fs::path descPath = pluginDir / "plugin.json"; fs::exists(descPath)) {
                            dirs.push_back(pluginDir);
                        }
                    }
                } catch (const std::exception &e) {
                    PluginLog.srtWarning("setPluginPaths: failed to scan plugin path %1: %2",
                                         stdc::path::to_utf8(canonicalPath), e.what());
                    continue;
                }
            }
            if (!dirs.empty()) {
                impl.m_pluginDirs[iid] = dirs;
            }
            if (!sharedDirs.empty()) {
                impl.m_sharedDirs[iid] = std::move(sharedDirs);
            }
        }

        impl.m_pluginsDirty.insert(iid);
    }

    std::vector<std::filesystem::path> PluginFactory::pluginPaths(const char *iid) const {
        stdc_impl_t;

        std::shared_lock<std::shared_mutex> lock(impl.m_plugins_mtx);
        auto                                it = impl.m_pluginDirs.find(iid);
        if (it == impl.m_pluginDirs.end()) {
            return {};
        }
        return {it->second.begin(), it->second.end()};
    }

    Plugin *PluginFactory::plugin(const char *iid, const char *key) const {
        stdc_impl_t;

        std::unique_lock<std::shared_mutex> lock(impl.m_plugins_mtx);
        if (impl.m_pluginsDirty.count(iid)) {
            impl.scanPlugins(iid);
        }

        auto it = impl.m_allPlugins.find(iid);
        if (it == impl.m_allPlugins.end()) {
            return nullptr;
        }

        const auto &pluginsMap = it->second;
        auto        it2        = pluginsMap.find(key);
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

} // namespace srt::core
