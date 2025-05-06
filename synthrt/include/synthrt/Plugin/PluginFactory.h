#ifndef SYNTHRT_PLUGINFACTORY_H
#define SYNTHRT_PLUGINFACTORY_H

#include <filesystem>
#include <vector>

#include <stdcorelib/adt/array_view.h>

#include <synthrt/Plugin/Plugin.h>

namespace srt {

    /// PluginFactory - Manages plugin loading and lifecycle.
    ///
    /// Plugins:
    ///  - dynamic plugins: shared libraries loaded from registered directories per \c iid
    ///  - static plugins :  class instances (not owned by PluginFactory)
    class SYNTHRT_EXPORT PluginFactory {
    public:
        PluginFactory();
        virtual ~PluginFactory();

    public:
        /// Adds a static plugin to the factory. A static plugin is usually a static instance which
        /// is not allocated on the heap.
        ///
        /// The memory ownership of static plugins is not transferred to the PluginFactory.
        void addStaticPlugin(Plugin *plugin);
        std::vector<Plugin *> staticPlugins() const;

        void addPluginPath(const char *iid, const std::filesystem::path &path);
        void setPluginPaths(const char *iid, stdc::array_view<std::filesystem::path> paths);
        stdc::array_view<std::filesystem::path> pluginPaths(const char *iid) const;

    public:
        Plugin *plugin(const char *iid, const char *key) const;

        template <class T>
        inline T *plugin(const char *key) const;

    protected:
        class Impl;
        std::unique_ptr<Impl> _impl;

        explicit PluginFactory(Impl &impl);
    };

    template <class T>
    inline T *PluginFactory::plugin(const char *key) const {
        static_assert(std::is_base_of<Plugin, T>::value, "T should inherit from srt::Plugin");
        return static_cast<T *>(plugin(reinterpret_cast<T *>(0)->T::iid(), key));
    }

}

#endif // SYNTHRT_PLUGINFACTORY_H