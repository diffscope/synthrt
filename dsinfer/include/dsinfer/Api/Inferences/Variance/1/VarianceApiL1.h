#ifndef DSINFER_API_VARIANCEAPIL1_H
#define DSINFER_API_VARIANCEAPIL1_H

#include <string>
#include <vector>
#include <map>
#include <set>
#include <filesystem>

#include <synthrt/SVS/InferenceContrib.h>
#include <synthrt/SVS/Inference.h>

#include <dsinfer/Core/ParamTag.h>
#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>

namespace ds::Api::Variance::L1 {

    using InputWordInfo = Common::L1::InputWordInfo;
    using InputParameterInfo = Common::L1::InputParameterInfo;
    using InputSpeakerInfo = Common::L1::InputSpeakerInfo;
    using LinguisticMode = Common::L1::LinguisticMode;

    inline constexpr char API_NAME[] = "variance";

    inline constexpr char API_CLASS[] = "ai.svs.VarianceInference";

    inline constexpr int API_LEVEL = 1;

    class VarianceSchema : public srt::InferenceSchema {
    public:
        inline VarianceSchema() : srt::InferenceSchema(API_NAME, API_CLASS, API_LEVEL) {
        }

        /// 说话人（音色）名称列表
        std::vector<std::string> speakers;

        /// 预测输出参数列表（顺序需和 ONNX 模型参数顺序一致）
        std::vector<ParamTag> predictions;
    };

    class VarianceConfiguration : public srt::InferenceConfiguration {
    public:
        inline VarianceConfiguration()
            : srt::InferenceConfiguration(API_NAME, API_CLASS, API_LEVEL) {
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

        /// 是否使用连续加速采样
        bool useContinuousAcceleration = true;
    };

    class VarianceImportOptions : public srt::InferenceImportOptions {
    public:
        inline VarianceImportOptions()
            : srt::InferenceImportOptions(API_NAME, API_CLASS, API_LEVEL) {
        }

        /// 歌手全局音色名称 => 模块内部嵌入名称映射
        std::map<std::string, std::string> speakerMapping;

        /// 预测输出参数列表
        std::set<ParamTag> predictions;
    };

    class VarianceRuntimeOptions : public srt::InferenceRuntimeOptions {
    public:
        inline VarianceRuntimeOptions()
            : srt::InferenceRuntimeOptions(API_NAME, API_CLASS, API_LEVEL) {
        }

        /// Reserved
    };

    class VarianceInitArgs : public srt::InferenceInitArgs {
    public:
        inline VarianceInitArgs() : InferenceInitArgs(API_NAME) {
        }

        /// Reserved
    };

    class VarianceStartInput : public srt::TaskStartInput {
    public:
        inline VarianceStartInput() : srt::TaskStartInput(API_NAME) {
        }

        double duration = 0;
        std::vector<InputWordInfo> words;
        std::vector<InputParameterInfo> parameters;
        std::vector<InputSpeakerInfo> speakers;

        int64_t steps = 0;
    };

    class VarianceResult : public srt::TaskResult {
    public:
        using InputParameterInfo = Common::L1::InputParameterInfo;

        inline VarianceResult() : srt::TaskResult(API_NAME) {
        }

        std::vector<InputParameterInfo> predictions;
    };

}

#endif // DSINFER_API_VARIANCEAPIL1_H