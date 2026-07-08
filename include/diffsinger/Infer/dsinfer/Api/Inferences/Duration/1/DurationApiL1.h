#ifndef DSINFER_API_DURATIONAPIL1_H
#define DSINFER_API_DURATIONAPIL1_H

#include <string>
#include <vector>
#include <map>
#include <filesystem>

#include <synthrt/SVS/InferenceInterpreter.h>
#include <synthrt/SVS/Inference.h>
#include <synthrt/Core/Task/ITask.h>

#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>

namespace srt::svs::Api::Duration::L1 {

    inline constexpr char API_NAME[] = "duration";

    inline constexpr char API_CLASS[] = "ai.svs.DurationInference";

    inline constexpr int API_LEVEL = 1;

    using InputWordInfo = Common::L1::InputWordInfo;

    class DurationSchema : public srt::svs::InferenceSchema {
    public:
        inline DurationSchema() : srt::svs::InferenceSchema(API_NAME, API_CLASS, API_LEVEL) {
        }

        /// 说话人（音色）名称列表
        std::vector<std::string> speakers;
    };

    class DurationConfiguration : public srt::svs::InferenceConfiguration {
    public:
        inline DurationConfiguration()
            : srt::svs::InferenceConfiguration(API_NAME, API_CLASS, API_LEVEL) {
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

    class DurationImportOptions : public srt::svs::InferenceImportOptions {
    public:
        inline DurationImportOptions()
            : srt::svs::InferenceImportOptions(API_NAME, API_CLASS, API_LEVEL) {
        }

        /// 歌手全局音色名称 => 模块内部嵌入名称映射
        std::map<std::string, std::string> speakerMapping;
    };

    class DurationRuntimeOptions : public srt::svs::InferenceRuntimeOptions {
    public:
        inline DurationRuntimeOptions()
            : srt::svs::InferenceRuntimeOptions(API_NAME, API_CLASS, API_LEVEL) {
        }

        /// Reserved
    };

    class DurationInitArgs : public srt::svs::InferenceInitArgs {
    public:
        inline DurationInitArgs() : InferenceInitArgs(API_NAME) {
        }

        /// Reserved
    };

    class DurationStartInput : public srt::core::TaskStartInput {
    public:
        inline DurationStartInput() : srt::core::TaskStartInput(API_NAME) {
        }

        double duration = 0;
        std::vector<InputWordInfo> words;
    };

    class DurationResult : public srt::core::TaskResult {
    public:
        inline DurationResult() : srt::core::TaskResult(API_NAME) {
        }

        std::vector<double> durations;
    };

}

#endif // DSINFER_API_DURATIONAPIL1_H