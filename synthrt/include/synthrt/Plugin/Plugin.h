#ifndef SYNTHRT_PLUGIN_H
#define SYNTHRT_PLUGIN_H

#include <filesystem>

#include <synthrt/synthrt_global.h>

namespace srt {

    class SYNTHRT_EXPORT Plugin {
    public:
        virtual ~Plugin();

    public:
        virtual const char *iid() const = 0;
        virtual const char *key() const = 0;

    public:
        std::filesystem::path path() const;
    };

}

#define DSINFER_EXPORT_PLUGIN(PLUGIN_NAME)                                                         \
    extern "C" STDCORELIB_DECL_EXPORT synthrt::Plugin *synthrt_plugin_instance() {                 \
        static PLUGIN_NAME _instance;                                                              \
        return &_instance;                                                                         \
    }

#endif // SYNTHRT_PLUGIN_H