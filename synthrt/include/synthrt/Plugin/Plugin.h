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

}

#define SYNTHRT_EXPORT_PLUGIN(PLUGIN_NAME)                                                         \
    extern "C" STDCORELIB_DECL_EXPORT srt::Plugin *synthrt_plugin_instance() {                     \
        static PLUGIN_NAME _instance;                                                              \
        return &_instance;                                                                         \
    }

#endif // SYNTHRT_PLUGIN_H