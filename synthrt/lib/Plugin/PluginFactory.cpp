#include "PluginFactory.h"
#include "PluginFactory_p.h"

#include <utility>
#include <cstring>
#include <mutex>

#include <stdcorelib/pimpl.h>
#include <stdcorelib/3rdparty/llvm/SmallVector.h>

namespace fs = std::filesystem;

namespace srt {

    using StaticPluginMap = std::map<std::string, llvm::SmallVector<StaticPlugin, 20>>;

    static StaticPluginMap &getStaticPluginMap() {
        static StaticPluginMap staticPluginMap;
        return staticPluginMap;
    }

    void StaticPlugin::registerStaticPlugin(const char *pluginSet, StaticPlugin plugin) {
        auto &plugins = getStaticPluginMap()[pluginSet];

        // insert the plugin in the list, sorted by address, so we can detect
        // duplicate registrations
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
        // Unload all libraries
        for (const auto &item : std::as_const(libraryInstances)) {
            delete item.second;
        }
    }

    void PluginFactory::Impl::scanPlugins(const char *iid) const {
        auto &plugins = allPlugins[iid];
        for (const auto &plugin : runtimePlugins) {
            if (strcmp(iid, plugin->iid()) == 0) {
                std::ignore = plugins.insert(std::make_pair(plugin->key(), plugin));
            }
        }

        auto it = pluginPaths.find(iid);
        if (it != pluginPaths.end()) {
            for (const auto &pluginPath : it->second) {
                for (const auto &entry : fs::directory_iterator(pluginPath)) {
                    const auto &entryPath = fs::canonical(entry.path());
                    if (libraryInstances.count(entryPath) ||
                        !stdc::SharedLibrary::isLibrary(entryPath)) {
                        continue;
                    }

                    stdc::SharedLibrary so;
                    if (!so.open(entryPath)) {
                        continue;
                    }

                    using PluginGetter = Plugin *(*) ();
                    auto getter =
                        reinterpret_cast<PluginGetter>(so.resolve("synthrt_plugin_instance"));
                    if (!getter) {
                        continue;
                    }

                    auto plugin = getter();
                    if (!plugin || strcmp(iid, plugin->iid()) != 0 ||
                        !plugins.insert(std::make_pair(plugin->key(), plugin)).second) {
                        continue;
                    }
                    libraryInstances[entryPath] = new stdc::SharedLibrary(std::move(so));
                }
            }
        }

        if (plugins.empty()) {
            allPlugins.erase(iid);
        }
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
        impl.pluginPaths[iid].push_back(fs::canonical(path));
        impl.pluginsDirty.insert(iid);
    }

    void PluginFactory::setPluginPaths(const char *iid,
                                       stdc::array_view<std::filesystem::path> paths) {
        __stdc_impl_t;
        std::unique_lock<std::shared_mutex> lock(impl.plugins_mtx);
        if (paths.empty()) {
            impl.pluginPaths.erase(iid);
        } else {
            llvm::SmallVector<fs::path> realPaths;
            realPaths.reserve(paths.size());
            for (const auto &path : paths) {
                if (fs::is_directory(path)) {
                    realPaths.push_back(fs::canonical(path));
                }
            }
            impl.pluginPaths[iid] = realPaths;
        }
        impl.pluginsDirty.insert(iid);
    }

    std::vector<std::filesystem::path> PluginFactory::pluginPaths(const char *iid) const {
        __stdc_impl_t;

        std::shared_lock<std::shared_mutex> lock(impl.plugins_mtx);
        auto it = impl.pluginPaths.find(iid);
        if (it == impl.pluginPaths.end()) {
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