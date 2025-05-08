#ifndef DSINFER_API_ACOUSTICAPIL1_H
#define DSINFER_API_ACOUSTICAPIL1_H

#include <synthrt/SVS/InferenceContrib.h>
#include <synthrt/SVS/Inference.h>

namespace ds::Api::Acoustic::L1 {

    static const char API_NAME[] = "acoustic";

    static const char API_IID[] = "ai.svs.AcousticInference";

    static const int API_LEVEL = 1;

    class AcousticSchema : public srt::InferenceSchema {
    public:
        AcousticSchema() : srt::InferenceSchema(API_NAME, API_IID, API_LEVEL) {
        }
    };

    class AcousticConfiguration : public srt::InferenceConfiguration {
    public:
        AcousticConfiguration() : srt::InferenceConfiguration(API_NAME, API_IID, API_LEVEL) {
        }
    };

    class AcousticImportOptions : public srt::InferenceImportOptions {
    public:
        AcousticImportOptions() : srt::InferenceImportOptions(API_NAME, API_IID, API_LEVEL) {
        }
    };

    class AcousticRuntimeOptions : public srt::InferenceRuntimeOptions {
    public:
        AcousticRuntimeOptions() : srt::InferenceRuntimeOptions(API_NAME, API_IID, API_LEVEL) {
        }
    };

    class AcousticInitArgs : public srt::InferenceInitArgs {
    public:
        AcousticInitArgs() : InferenceInitArgs(API_NAME) {
        }
    };

}

#endif // DSINFER_API_ACOUSTICAPIL1_H