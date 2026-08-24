#ifndef DSINFER_API_VOCODERAPIL1_H
#define DSINFER_API_VOCODERAPIL1_H

#include <synthrt/SVS/InferenceContrib.h>
#include <synthrt/SVS/Inference.h>

#include <dsinfer/Core/Tensor.h>
#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>

namespace ds::Api::Vocoder::L1 {

    inline constexpr char API_INTERFACE[] = "org.openvpi.svs.VocoderInference";

    inline constexpr char API_VARIANT[] = "onnx";

    inline constexpr int API_LEVEL = 1;

    using MelBase = Common::L1::MelBase;

    using MelScale = Common::L1::MelScale;

    class VocoderImportOptions : public srt::ContribImportOptions {
    public:
        inline VocoderImportOptions()
            : srt::ContribImportOptions(API_INTERFACE, API_VARIANT, API_LEVEL) {
        }

        // TODO
    };

    class VocoderRuntimeOptions : public srt::InferenceRuntimeOptions {
    public:
        inline VocoderRuntimeOptions()
            : srt::InferenceRuntimeOptions(API_INTERFACE, API_VARIANT, API_LEVEL) {
        }

        /// Reserved
    };

    class VocoderSchema : public srt::ContribExports {
    public:
        inline VocoderSchema() : srt::ContribExports(API_INTERFACE, API_VARIANT, API_LEVEL) {
        }

        /// Reserved
    };

    class VocoderConfiguration : public srt::ContribConfiguration {
    public:
        inline VocoderConfiguration()
            : srt::ContribConfiguration(API_INTERFACE, API_VARIANT, API_LEVEL) {
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

    class VocoderInitArgs : public srt::InferenceInitArgs {
    public:
        inline VocoderInitArgs() : InferenceInitArgs(API_INTERFACE, API_LEVEL) {
        }

        /// Reserved
    };

    class VocoderStartInput : public srt::TaskStartInput {
    public:
        inline VocoderStartInput() : srt::TaskStartInput(API_INTERFACE, API_LEVEL) {
        }

        std::shared_ptr<ITensor> mel;
        std::shared_ptr<ITensor> f0;
    };

    class VocoderResult : public srt::TaskResult {
    public:
        inline VocoderResult() : srt::TaskResult(API_INTERFACE, API_LEVEL) {
        }

        std::vector<uint8_t> audioData;
    };

}

#endif // DSINFER_API_VOCODERAPIL1_H
