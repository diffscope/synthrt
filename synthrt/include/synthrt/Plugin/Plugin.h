#ifndef SYNTHRT_PLUGIN_H
#define SYNTHRT_PLUGIN_H

#include <filesystem>

#include <synthrt/synthrt_global.h>

namespace srt {

    /// Plugin - Base class for all plugins.
    class SYNTHRT_EXPORT Plugin {
    public:
        virtual ~Plugin();

    public:
        /// Returns the interface identifier of the plugin.
        virtual const char *iid() const = 0;

        /// Returns the key of the plugin.
        virtual const char *key() const = 0;

    public:
        std::filesystem::path path() const;
    };

    class StaticPlugin {
    public:
        using PluginInstanceFunction = Plugin *(*) ();

        constexpr StaticPlugin(PluginInstanceFunction i) : instance(i) {
        }

        PluginInstanceFunction instance = nullptr;

    public:
        SYNTHRT_EXPORT static void registerStaticPlugin(const char *pluginSet, StaticPlugin plugin);
    };

}

#define SYNTHRT_EXPORT_PLUGIN(PLUGIN_NAME)                                                         \
    extern "C" STDCORELIB_DECL_EXPORT srt::Plugin *synthrt_plugin_instance() {                     \
        static PLUGIN_NAME _instance;                                                              \
        return &_instance;                                                                         \
    }

#define SYNTHRT_EXPORT_STATIC_PLUGIN(PLUGIN_NAME, PLUGIN_SET)                                      \
    struct initializer {                                                                           \
        initializer() {                                                                            \
            srt::StaticPlugin::registerStaticPlugin(PLUGIN_SET,                                    \
                                                    srt::StaticPlugin([]() -> srt::Plugin * {      \
                                                        static PLUGIN_NAME _instance;              \
                                                        return &_instance;                         \
                                                    }));                                           \
        }                                                                                          \
        ~initializer() {                                                                           \
        }                                                                                          \
    } dummy;

#endif // SYNTHRT_PLUGIN_H