#ifndef DSINFER_API_ACOUSTICAPIL1_H
#define DSINFER_API_ACOUSTICAPIL1_H

#include <string>
#include <vector>
#include <map>
#include <set>
#include <filesystem>

#include <synthrt/SVS/InferenceContrib.h>
#include <synthrt/SVS/Inference.h>

#include <dsinfer/Core/Tensor.h>
#include <dsinfer/Core/ParamTag.h>
#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>

namespace ds::Api::Acoustic::L1 {

    static constexpr char API_NAME[] = "acoustic";

    static constexpr char API_CLASS[] = "ai.svs.AcousticInference";

    static constexpr int API_LEVEL = 1;

    class AcousticSchema : public srt::InferenceSchema {
    public:
        inline AcousticSchema() : srt::InferenceSchema(API_NAME, API_CLASS, API_LEVEL) {
        }

        /// 说话人（音色）名称列表
        std::vector<std::string> speakers;

        /// 需要输入的唱法参数列表
        std::set<ParamTag> varianceControls;

        /// 支持的偏移变换类型参数列表
        std::set<ParamTag> transitionControls;
    };

    class AcousticConfiguration : public srt::InferenceConfiguration {
    public:
        using MelBase = Common::L1::MelBase;
        using MelScale = Common::L1::MelScale;

        inline AcousticConfiguration()
            : srt::InferenceConfiguration(API_NAME, API_CLASS, API_LEVEL) {
        }

        /// 音素名称与音素 ID 对应表或存储对应信息
        std::map<std::string, int> phonemes;

        /// 语言名称与语言 ID 对应表或存储对应信息
        std::map<std::string, int> languages;

        /// 说话人（音色）与说话人嵌入文件路径对应表
        std::map<std::string, std::filesystem::path> speakers;

        /// 声学模型文件路径
        std::filesystem::path model;

        /// 是否启用语言 ID 嵌入
        bool useLanguageId = false;

        /// 是否启用说话人嵌入
        bool useSpeakerEmbedding = false;

        /// 隐层维度（说话人嵌入向量维度）
        int hiddenSize = 256;

        /// 启用的参数列表
        std::set<ParamTag> parameters;

        /// 是否使用连续加速采样
        bool useContinuousAcceleration = false;

        /// 是否使用可变深度采样
        bool useVariableDepth = false;

        /// 允许的最大深度
        double maxDepth = 0.0;

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
    };

    class AcousticImportOptions : public srt::InferenceImportOptions {
    public:
        inline AcousticImportOptions()
            : srt::InferenceImportOptions(API_NAME, API_CLASS, API_LEVEL) {
        }

        /// 歌手全局音色名称 => 模块内部嵌入名称映射
        std::map<std::string, std::string> speakerMapping;
    };

    class AcousticRuntimeOptions : public srt::InferenceRuntimeOptions {
    public:
        inline AcousticRuntimeOptions()
            : srt::InferenceRuntimeOptions(API_NAME, API_CLASS, API_LEVEL) {
        }

        /// Reserved
    };

    class AcousticInitArgs : public srt::InferenceInitArgs {
    public:
        inline AcousticInitArgs() : InferenceInitArgs(API_NAME) {
        }

        /// Reserved
    };

    class AcousticStartInput : public srt::TaskStartInput {
    public:
        using InputWordInfo = Common::L1::InputWordInfo;
        using InputParameterInfo = Common::L1::InputParameterInfo;
        using InputSpeakerInfo = Common::L1::InputSpeakerInfo;

        inline AcousticStartInput() : srt::TaskStartInput(API_NAME) {
        }

        double duration = 0;
        std::vector<InputWordInfo> words;
        std::vector<InputParameterInfo> parameters;
        std::vector<InputSpeakerInfo> speakers;

        float depth = 0;
        int64_t steps = 0;
    };

    class AcousticResult : public srt::TaskResult {
    public:
        inline AcousticResult() : srt::TaskResult(API_NAME) {
        }

        srt::NO<ITensor> mel;
        srt::NO<ITensor> f0;
    };

}

#endif // DSINFER_API_ACOUSTICAPIL1_H