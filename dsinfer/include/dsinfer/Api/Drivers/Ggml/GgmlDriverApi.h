#ifndef DSINFER_API_GGML_GGMLDRIVERAPI_H
#define DSINFER_API_GGML_GGMLDRIVERAPI_H

#include <dsinfer/Api/Drivers/Common/CommonDriverApi.h>

namespace ds::Api::Ggml {

    inline constexpr char API_NAME[] = "ggml";

    inline constexpr int API_VERSION = 1;

    class DriverInitArgs : public InferenceDriverInitArgs {
    public:
        inline DriverInitArgs() : InferenceDriverInitArgs(API_NAME, API_VERSION) {
        }

        int threads = 4;

        bool useGpu = false;
    };

    class SessionOpenArgs : public Common::SessionOpenArgs {
    public:
        inline SessionOpenArgs() : Common::SessionOpenArgs() {
            setObjectName(API_NAME);
        }

        bool useF16 = false;
    };

    using SessionStartInput = Common::SessionStartInput;

    using SessionResult = Common::SessionResult;

}

#endif // DSINFER_API_GGML_GGMLDRIVERAPI_H
