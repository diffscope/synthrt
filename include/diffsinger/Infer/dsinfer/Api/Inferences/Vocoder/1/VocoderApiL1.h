#ifndef DSINFER_API_VOCODERAPIL1_H
#define DSINFER_API_VOCODERAPIL1_H

#include <synthrt/SVS/InferenceInterpreter.h>
#include <synthrt/SVS/Inference.h>

#include <synthrt/Core/Tensor/ITensor.h>
#include <synthrt/Core/Tensor/Tensor.h>
#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>

namespace srt::svs::Api::Vocoder::L1 {

    inline constexpr char API_NAME[] = "vocoder";

    inline constexpr char API_CLASS[] = "ai.svs.VocoderInference";

    inline constexpr int API_LEVEL = 1;

    using MelBase = Common::L1::MelBase;

    using MelScale = Common::L1::MelScale;

    class VocoderImportOptions : public srt::svs::InferenceImportOptions {
    public:
        inline VocoderImportOptions()
            : srt::svs::InferenceImportOptions(API_NAME, API_CLASS, API_LEVEL) {
        }

        // TODO
    };

    class VocoderRuntimeOptions : public srt::svs::InferenceRuntimeOptions {
    public:
        inline VocoderRuntimeOptions()
            : srt::svs::InferenceRuntimeOptions(API_NAME, API_CLASS, API_LEVEL) {
        }

        /// Reserved
    };

    class VocoderSchema : public srt::svs::InferenceSchema {
    public:
        inline VocoderSchema() : srt::svs::InferenceSchema(API_NAME, API_CLASS, API_LEVEL) {
        }

        /// Reserved
    };

    class VocoderConfiguration : public srt::svs::InferenceConfiguration {
    public:
        inline VocoderConfiguration()
            : srt::svs::InferenceConfiguration(API_NAME, API_CLASS, API_LEVEL) {
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
        MelBase melBase = MelBase::MelBase_E;

        /// melScale
        MelScale melScale = MelScale::MelScale_Slaney;

        /// 是否支持与声学模型不同的音高输入
        bool pitchControllable = false;
    };

    class VocoderInitArgs : public srt::svs::InferenceInitArgs {
    public:
        inline VocoderInitArgs() : InferenceInitArgs(API_NAME) {
        }

        /// Reserved
    };

    class VocoderStartInput : public srt::core::TaskStartInput {
    public:
        inline VocoderStartInput() : srt::core::TaskStartInput(API_NAME) {
        }

        srt::core::NO<srt::core::ITensor> mel;
        srt::core::NO<srt::core::ITensor> f0;
    };

    class VocoderResult : public srt::core::TaskResult {
    public:
        inline VocoderResult() : srt::core::TaskResult(API_NAME) {
        }

        std::vector<uint8_t> audioData;
    };

}

#endif // DSINFER_API_VOCODERAPIL1_H