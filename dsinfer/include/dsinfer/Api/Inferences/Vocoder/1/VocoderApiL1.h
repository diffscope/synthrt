#ifndef DSINFER_API_VOCODERAPIL1_H
#define DSINFER_API_VOCODERAPIL1_H

#include <synthrt/SVS/InferenceContrib.h>
#include <synthrt/SVS/Inference.h>

#include <dsinfer/Core/Tensor.h>

namespace ds::Api::Vocoder::L1 {

    static constexpr char API_NAME[] = "vocoder";

    static constexpr char API_CLASS[] = "ai.svs.VocoderInference";

    static constexpr int API_LEVEL = 1;

    class VocoderImportOptions : public srt::InferenceImportOptions {
    public:
        inline VocoderImportOptions()
            : srt::InferenceImportOptions(API_NAME, API_CLASS, API_LEVEL) {
        }

        // TODO
    };

    class VocoderRuntimeOptions : public srt::InferenceRuntimeOptions {
    public:
        inline VocoderRuntimeOptions()
            : srt::InferenceRuntimeOptions(API_NAME, API_CLASS, API_LEVEL) {
        }

        /// Reserved
    };

    class VocoderInitArgs : public srt::InferenceInitArgs {
    public:
        inline VocoderInitArgs() : InferenceInitArgs(API_NAME) {
        }

        /// Reserved
    };

    class VocoderStartInput : public srt::TaskStartInput {
    public:
        inline VocoderStartInput() : srt::TaskStartInput(API_NAME) {
        }

        srt::NO<AbstractTensor> mel;
    };

    class VocoderResult : public srt::TaskResult {
    public:
        inline VocoderResult() : srt::TaskResult(API_NAME) {
        }

        std::vector<uint8_t> audioData;
    };

}

#endif // DSINFER_API_VOCODERAPIL1_H