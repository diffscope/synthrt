#ifndef DSINFER_API_COMMON_COMMONDRIVERAPI_H
#define DSINFER_API_COMMON_COMMONDRIVERAPI_H

#include <filesystem>
#include <map>
#include <set>

#include <dsinfer/Core/Tensor.h>
#include <dsinfer/Inference/InferenceDriver.h>
#include <dsinfer/Inference/InferenceSession.h>

namespace ds::Api::Common {

    inline constexpr char API_NAME[] = "common";

    inline constexpr int API_VERSION = 1;

    class DriverInitArgs : public InferenceDriverInitArgs {
    public:
        inline DriverInitArgs() : InferenceDriverInitArgs(API_NAME, API_VERSION) {
        }
    };

    class SessionOpenArgs : public InferenceSessionOpenArgs {
    public:
        inline SessionOpenArgs() : InferenceSessionOpenArgs(API_NAME, API_VERSION) {
        }

        bool useCpu = false;
    };

    class SessionStartInput : public InferenceSessionStartInput {
    public:
        inline SessionStartInput() : InferenceSessionStartInput(API_NAME, API_VERSION) {
        }

        std::map<std::string, srt::NO<ITensor>> inputs;

        std::set<std::string> outputs;
    };

    class SessionResult : public InferenceSessionResult {
    public:
        inline SessionResult() : InferenceSessionResult(API_NAME, API_VERSION) {
        }

        std::map<std::string, srt::NO<ITensor>> outputs;
    };

}

#endif // DSINFER_API_COMMON_COMMONDRIVERAPI_H
