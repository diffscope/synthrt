#ifndef DSINFER_API_VARIANCEAPIL1_H
#define DSINFER_API_VARIANCEAPIL1_H

#include <string>
#include <vector>
#include <map>
#include <set>
#include <filesystem>

#include <synthrt/SVS/InferenceContrib.h>
#include <synthrt/SVS/InferenceExecInstance.h>

#include <dsinfer/Core/ParamTag.h>
#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>

namespace ds::Api::Variance::L1 {

    using InputWordInfo = Common::L1::InputWordInfo;
    using InputParameterInfo = Common::L1::InputParameterInfo;
    using InputSpeakerInfo = Common::L1::InputSpeakerInfo;
    using LinguisticMode = Common::L1::LinguisticMode;

    /// Identifies the variance inference contract.
    inline constexpr char API_INTERFACE[] = "org.openvpi.svs.VarianceInference";

    /// Identifies the ONNX implementation variant.
    inline constexpr char API_VARIANT[] = "onnx";

    /// Identifies Level 1 of the variance inference contract.
    inline constexpr int API_LEVEL = 1;

    /// Describes the capabilities exported by a variance inference contribution.
    class VarianceSchema : public srt::ContribExports {
    public:
        inline VarianceSchema() : srt::ContribExports(API_INTERFACE, API_VARIANT, API_LEVEL) {
        }

        /// Speaker identifiers accepted by this contribution.
        std::vector<std::string> speakers;

        /// Parameters predicted by the model in model output order.
        std::vector<ParamTag> predictions;
    };

    /// Contains the interpreted configuration of an ONNX variance model.
    class VarianceConfiguration : public srt::ContribConfiguration {
    public:
        inline VarianceConfiguration()
            : srt::ContribConfiguration(API_INTERFACE, API_VARIANT, API_LEVEL) {
        }

        /// Maps phoneme tokens to model input identifiers.
        std::map<std::string, int> phonemes;

        /// Maps language identifiers to model input identifiers.
        std::map<std::string, int> languages;

        /// Maps speaker identifiers to their embedding vectors.
        std::map<std::string, std::vector<float>> speakers;

        /// Path of the linguistic encoder model.
        std::filesystem::path encoder;

        /// Path of the variance predictor model.
        std::filesystem::path predictor;

        /// Duration of one model frame in seconds.
        double frameWidth = 512.0 / 44100.0;

        /// Granularity accepted by the linguistic encoder.
        LinguisticMode linguisticMode = LinguisticMode::Phoneme;

        /// Width of the encoder state and each speaker embedding vector.
        int hiddenSize = 256;

        /// Indicates whether the encoder consumes language identifiers.
        bool useLanguageId = false;

        /// Indicates whether the predictor consumes speaker embeddings.
        bool useSpeakerEmbedding = false;

        /// Selects continuous sampling acceleration instead of discrete acceleration.
        bool useContinuousAcceleration = true;
    };

    /// Configures one import of a variance inference contribution.
    class VarianceImportOptions : public srt::ContribImportOptions {
    public:
        inline VarianceImportOptions()
            : srt::ContribImportOptions(API_INTERFACE, API_VARIANT, API_LEVEL) {
        }

        /// Maps singer level speaker identifiers to contribution level identifiers.
        std::map<std::string, std::string> speakerMapping;

        /// Selects the exported parameters consumed by the importer.
        std::set<ParamTag> predictions;
    };

    /// Contains runtime options used when creating a variance inference instance.
    class VarianceRuntimeOptions : public srt::InferenceRuntimeOptions {
    public:
        inline VarianceRuntimeOptions()
            : srt::InferenceRuntimeOptions(API_INTERFACE, API_VARIANT, API_LEVEL) {
        }
    };

    /// Contains arguments used to initialize a variance inference instance.
    class VarianceInitArgs : public srt::InferenceInitArgs {
    public:
        inline VarianceInitArgs() : InferenceInitArgs(API_INTERFACE, API_LEVEL) {
        }
    };

    /// Supplies score, control curves, and sampling parameters for variance prediction.
    class VarianceStartInput : public srt::TaskStartInput {
    public:
        inline VarianceStartInput() : srt::TaskStartInput(API_INTERFACE, API_LEVEL) {
        }

        /// Total input duration in seconds.
        double duration = 0;

        /// Words and notes that define the score.
        std::vector<InputWordInfo> words;

        /// Existing parameter curves used as prediction conditions.
        std::vector<InputParameterInfo> parameters;

        /// Speaker mixture curves used as prediction conditions.
        std::vector<InputSpeakerInfo> speakers;

        /// Number of sampling steps used by continuous acceleration.
        int64_t steps = 0;
    };

    /// Contains the parameter curves produced by variance prediction.
    class VarianceResult : public srt::TaskResult {
    public:
        using InputParameterInfo = Common::L1::InputParameterInfo;

        inline VarianceResult() : srt::TaskResult(API_INTERFACE, API_LEVEL) {
        }

        /// Predicted parameter curves in the order declared by the contribution.
        std::vector<InputParameterInfo> predictions;
    };

}

#endif // DSINFER_API_VARIANCEAPIL1_H
