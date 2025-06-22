#ifndef DSINFER_API_DURATIONAPIL1_H
#define DSINFER_API_DURATIONAPIL1_H

#include <string>
#include <vector>
#include <map>
#include <filesystem>

#include <synthrt/SVS/InferenceContrib.h>
#include <synthrt/SVS/Inference.h>

#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>

namespace ds::Api::Duration::L1 {

    inline constexpr char API_NAME[] = "duration";

    inline constexpr char API_CLASS[] = "ai.svs.DurationInference";

    inline constexpr int API_LEVEL = 1;

    class DurationSchema : public srt::InferenceSchema {
    public:
        inline DurationSchema() : srt::InferenceSchema(API_NAME, API_CLASS, API_LEVEL) {
        }

        /// 说话人（音色）名称列表
        std::vector<std::string> speakers;
    };

    class DurationConfiguration : public srt::InferenceConfiguration {
    public:
        inline DurationConfiguration()
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

        /// 是否启用语言 ID 嵌入
        bool useLanguageId = false;

        /// 是否启用说话人嵌入
        bool useSpeakerEmbedding = false;

        /// 隐层维度（说话人嵌入向量维度）
        int hiddenSize = 256;
    };

    class DurationImportOptions : public srt::InferenceImportOptions {
    public:
        inline DurationImportOptions()
            : srt::InferenceImportOptions(API_NAME, API_CLASS, API_LEVEL) {
        }

        /// 歌手全局音色名称 => 模块内部嵌入名称映射
        std::map<std::string, std::string> speakerMapping;
    };

    class DurationRuntimeOptions : public srt::InferenceRuntimeOptions {
    public:
        inline DurationRuntimeOptions()
            : srt::InferenceRuntimeOptions(API_NAME, API_CLASS, API_LEVEL) {
        }

        /// Reserved
    };

    class DurationInitArgs : public srt::InferenceInitArgs {
    public:
        inline DurationInitArgs() : InferenceInitArgs(API_NAME) {
        }

        /// Reserved
    };

    class DurationStartInput : public srt::TaskStartInput {
    public:
        using InputWordInfo = Common::L1::InputWordInfo;

        inline DurationStartInput() : srt::TaskStartInput(API_NAME) {
        }

        double duration = 0;
        std::vector<InputWordInfo> words;
    };

    class DurationResult : public srt::TaskResult {
    public:
        inline DurationResult() : srt::TaskResult(API_NAME) {
        }

        std::vector<double> durations;
        double interval = 0;
    };

}

#endif // DSINFER_API_DURATIONAPIL1_H