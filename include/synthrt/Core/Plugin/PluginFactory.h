#ifndef SRT_CORE_PLUGIN_PLUGINFACTORY_H
#define SRT_CORE_PLUGIN_PLUGINFACTORY_H

#include <filesystem>
#include <vector>

#include <stdcorelib/adt/array_view.h>

#include <synthrt/Core/Plugin/Plugin.h>

namespace srt::core {

    /// PluginFactory - Manages plugin loading and lifecycle.
    ///
    /// Plugins:
    ///  - filesystem plugins: shared libraries loaded from registered directories per \c iid
    ///  - runtime plugins   : runtime class instances (not owned by PluginFactory)
    ///  - static plugins    : static class instances (not owned by PluginFactory)
    class SRT_CORE_EXPORT PluginFactory {
    public:
        PluginFactory();
        virtual ~PluginFactory();

    public:
        static std::vector<std::string> staticPluginSets();
        static std::vector<StaticPlugin> staticPlugins(const char *pluginSet);
        static std::vector<Plugin *> staticInstances(const char *pluginSet);

    public:
        void addRuntimePlugin(Plugin *plugin);
        std::vector<Plugin *> runtimePlugins() const;

        void addPluginPath(const char *iid, const std::filesystem::path &path);
        /// Replaces discovery paths for plugins not yet bound to a key.
        /// Already loaded plugins and their libraries remain valid until this factory is destroyed;
        /// changing paths does not hot-reload or unload them.
        void setPluginPaths(const char *iid, stdc::array_view<std::filesystem::path> paths);
        std::vector<std::filesystem::path> pluginPaths(const char *iid) const;

    public:
        Plugin *plugin(const char *iid, const char *key) const;

        template <class T>
        inline T *plugin(const char *key) const;

    protected:
        class Impl;
        std::unique_ptr<Impl> _impl;

        explicit PluginFactory(Impl &impl);

        STDCORELIB_DISABLE_COPY_MOVE(PluginFactory);
    };

    template <class T>
    inline T *PluginFactory::plugin(const char *key) const {
        static_assert(std::is_base_of<Plugin, T>::value, "T should inherit from srt::core::Plugin");
        return static_cast<T *>(plugin(T::staticIid(), key));
    }

}

#endif // SRT_CORE_PLUGIN_PLUGINFACTORY_H
