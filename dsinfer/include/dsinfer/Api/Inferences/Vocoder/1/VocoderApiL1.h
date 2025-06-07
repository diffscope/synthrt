#ifndef DSINFER_API_VOCODERAPIL1_H
#define DSINFER_API_VOCODERAPIL1_H

#include <synthrt/SVS/InferenceContrib.h>
#include <synthrt/SVS/Inference.h>

#include <dsinfer/Core/Tensor.h>

namespace ds::Api::Vocoder::L1 {

    static constexpr char API_NAME[] = "vocoder";

    static constexpr char API_CLASS[] = "ai.svs.VocoderInference";

    static constexpr int API_LEVEL = 1;

    enum MelBase {
        MelBase_E,
        MelBase_10,
    };

    enum MelScale {
        MelScale_Slaney,
        MelScale_HTK,
    };

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

    class VocoderSchema : public srt::InferenceSchema {
    public:
        inline VocoderSchema() : srt::InferenceSchema(API_NAME, API_CLASS, API_LEVEL) {
        }

        /// Reserved
    };

    class VocoderConfiguration : public srt::InferenceConfiguration {
    public:
        inline VocoderConfiguration()
            : srt::InferenceConfiguration(API_NAME, API_CLASS, API_LEVEL) {
        }

        /// 声码器模型文件路径
        std::filesystem::path model;

        /// 音频采样率
        int sampleRate = 44100;

        /// 梅尔频谱帧跨度
        int hopSize = 2048;

        /// 梅尔频谱窗大小
        int winSize = 2048;

        /// 梅尔频谱 FFT 维度
        int fftSize = 128;

        /// 梅尔频谱通道数
        int melChannels = 128;

        /// 梅尔频谱最小频率（Hz）
        int melMinFreq = 0;

        /// 梅尔频谱最大频率（Hz）
        int melMaxFreq = 0;

        /// 梅尔频谱底数
        MelBase melBase = MelBase_E;

        /// melScale
        MelScale melScale = MelScale_Slaney;
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

        srt::NO<ITensor> mel;
    };

    class VocoderResult : public srt::TaskResult {
    public:
        inline VocoderResult() : srt::TaskResult(API_NAME) {
        }

        std::vector<uint8_t> audioData;
    };

}

#endif // DSINFER_API_VOCODERAPIL1_H