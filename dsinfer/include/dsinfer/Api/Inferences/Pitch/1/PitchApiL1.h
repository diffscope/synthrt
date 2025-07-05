#ifndef DSINFER_API_PITCHAPIL1_H
#define DSINFER_API_PITCHAPIL1_H

#include <string>
#include <vector>
#include <map>
#include <filesystem>

#include <synthrt/SVS/InferenceContrib.h>
#include <synthrt/SVS/Inference.h>

#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>

namespace ds::Api::Pitch::L1 {

    inline constexpr char API_NAME[] = "pitch";

    inline constexpr char API_CLASS[] = "ai.svs.PitchInference";

    inline constexpr int API_LEVEL = 1;

    using LinguisticMode = Common::L1::LinguistMode;
    using InputWordInfo = Common::L1::InputWordInfo;
    using InputParameterInfo = Common::L1::InputParameterInfo;
    using InputSpeakerInfo = Common::L1::InputSpeakerInfo;

    class PitchSchema : public srt::InferenceSchema {
    public:
        inline PitchSchema() : srt::InferenceSchema(API_NAME, API_CLASS, API_LEVEL) {
        }

        /// 说话人（音色）名称列表
        std::vector<std::string> speakers;

        /// 是否允许控制表现力因子
        bool allowExpressiveness = true;
    };

    class PitchConfiguration : public srt::InferenceConfiguration {
    public:
        inline PitchConfiguration() : srt::InferenceConfiguration(API_NAME, API_CLASS, API_LEVEL) {
        }

        /// 音素名称与音素 ID 对应表或存储对应信息
        std::map<std::string, int> phonemes;

        /// 语言名称与语言 ID 对应表或存储对应信息
        std::map<std::string, int> languages;

        /// 说话人（音色）与说话人嵌入向量对应表
        std::map<std::string, std::vector<float>> speakers;

        /// 编码器文件路径
        std::filesystem::path encoder;

        /// 预测器文件路径
        std::filesystem::path predictor;

        /// 帧宽度（秒）
        double frameWidth = 512.0 / 44100.0;

        /// 语言学编码器的工作模式（word 或 phoneme）
        LinguisticMode linguisticMode = LinguisticMode::LM_Phoneme;

        /// 隐层维度（说话人嵌入向量维度）
        int hiddenSize = 256;

        /// 是否启用语言 ID 嵌入
        bool useLanguageId = false;

        /// 是否启用说话人嵌入
        bool useSpeakerEmbedding = false;

        /// 是否启用表现力因子输入
        bool useExpressiveness = true;

        /// 是否启用休止符记号输入
        bool useRestFlags = true;

        /// 是否使用连续加速采样
        bool useContinuousAcceleration = true;
    };

    class PitchImportOptions : public srt::InferenceImportOptions {
    public:
        inline PitchImportOptions() : srt::InferenceImportOptions(API_NAME, API_CLASS, API_LEVEL) {
        }

        /// 歌手全局音色名称 => 模块内部嵌入名称映射
        std::map<std::string, std::string> speakerMapping;
    };

    class PitchRuntimeOptions : public srt::InferenceRuntimeOptions {
    public:
        inline PitchRuntimeOptions()
            : srt::InferenceRuntimeOptions(API_NAME, API_CLASS, API_LEVEL) {
        }

        /// Reserved
    };

    class PitchInitArgs : public srt::InferenceInitArgs {
    public:
        inline PitchInitArgs() : InferenceInitArgs(API_NAME) {
        }

        /// Reserved
    };

    class PitchStartInput : public srt::TaskStartInput {
    public:
        inline PitchStartInput() : srt::TaskStartInput(API_NAME) {
        }

        double duration = 0;
        std::vector<InputWordInfo> words;
        std::vector<InputParameterInfo> parameters;
        std::vector<InputSpeakerInfo> speakers;

        int64_t steps = 0;
    };

    class PitchResult : public srt::TaskResult {
    public:
        inline PitchResult() : srt::TaskResult(API_NAME) {
        }

        std::vector<double> pitch;
        double interval = 0;
    };

}

#endif // DSINFER_API_PITCHAPIL1_H