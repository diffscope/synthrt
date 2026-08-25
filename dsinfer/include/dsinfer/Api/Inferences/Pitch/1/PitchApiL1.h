#ifndef DSINFER_API_PITCHAPIL1_H
#define DSINFER_API_PITCHAPIL1_H

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <filesystem>

#include <synthrt/SVS/InferenceContrib.h>
#include <synthrt/SVS/InferenceExecInstance.h>

#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>

namespace ds::Api::Pitch::L1 {

    /// Identifies the pitch inference contract.
    inline constexpr char API_INTERFACE[] = "org.openvpi.svs.PitchInference";

    /// Identifies the ONNX implementation variant.
    inline constexpr char API_VARIANT[] = "onnx";

    /// Identifies Level 1 of the pitch inference contract.
    inline constexpr int API_LEVEL = 1;

    using LinguisticMode = Common::L1::LinguisticMode;
    using InputWordInfo = Common::L1::InputWordInfo;
    using InputParameterInfo = Common::L1::InputParameterInfo;
    using InputSpeakerInfo = Common::L1::InputSpeakerInfo;

    /// Describes the capabilities exported by a pitch inference contribution.
    class PitchSchema : public srt::ContribExports {
    public:
        inline PitchSchema() : srt::ContribExports(API_INTERFACE, API_VARIANT, API_LEVEL) {
        }

        /// Speaker identifiers accepted by this contribution.
        std::vector<std::string> speakers;

        /// Indicates whether the contribution accepts an expressiveness curve.
        bool allowExpressiveness = true;
    };

    /// Contains the interpreted configuration of an ONNX pitch model.
    class PitchConfiguration : public srt::ContribConfiguration {
    public:
        inline PitchConfiguration()
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

        /// Path of the pitch predictor model.
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

        /// Indicates whether the predictor consumes an expressiveness curve.
        bool useExpressiveness = true;

        /// Indicates whether the predictor consumes explicit rest flags.
        bool useRestFlags = true;

        /// Selects continuous sampling acceleration instead of discrete acceleration.
        bool useContinuousAcceleration = true;
    };

    /// Configures one import of a pitch inference contribution.
    class PitchImportOptions : public srt::ContribImportOptions {
    public:
        inline PitchImportOptions()
            : srt::ContribImportOptions(API_INTERFACE, API_VARIANT, API_LEVEL) {
        }

        /// Maps singer level speaker identifiers to contribution level identifiers.
        std::map<std::string, std::string> speakerMapping;
    };

    /// Contains runtime options used when creating a pitch inference instance.
    class PitchRuntimeOptions : public srt::InferenceRuntimeOptions {
    public:
        inline PitchRuntimeOptions()
            : srt::InferenceRuntimeOptions(API_INTERFACE, API_VARIANT, API_LEVEL) {
        }
    };

    /// Contains arguments used to initialize a pitch inference instance.
    class PitchInitArgs : public srt::InferenceInitArgs {
    public:
        inline PitchInitArgs() : InferenceInitArgs(API_INTERFACE, API_LEVEL) {
        }
    };

    /// Supplies score, control curves, and sampling parameters for pitch prediction.
    class PitchStartInput : public srt::TaskStartInput {
    public:
        inline PitchStartInput() : srt::TaskStartInput(API_INTERFACE, API_LEVEL) {
        }

        /// Total input duration in seconds.
        double duration = 0;

        /// Words and notes that define the score.
        std::vector<InputWordInfo> words;

        /// Parameter curves used as prediction conditions.
        std::vector<InputParameterInfo> parameters;

        /// Speaker mixture curves used as prediction conditions.
        std::vector<InputSpeakerInfo> speakers;

        /// Number of sampling steps used by continuous acceleration.
        int64_t steps = 0;
    };

    /// Contains the predicted pitch curve.
    class PitchResult : public srt::TaskResult {
    public:
        inline PitchResult() : srt::TaskResult(API_INTERFACE, API_LEVEL) {
        }

        /// Predicted pitch samples in semitones.
        std::vector<double> pitch;

        /// Time between adjacent pitch samples in seconds.
        double interval = 0;
    };

    /// Executes one pitch model using Level 1 typed payloads.
    ///
    /// An interpreter implementing this contract must create instances derived from this class.
    class PitchExecInstance : public srt::InferenceExecInstance {
    public:
        using AsyncCallback =
            std::function<void(srt::Expected<std::unique_ptr<PitchResult>> result)>;

        /// Initializes the pitch model instance.
        virtual srt::Expected<void> initialize(const PitchInitArgs &args) = 0;

        /// Executes pitch inference synchronously.
        virtual srt::Expected<std::unique_ptr<PitchResult>> start(const PitchStartInput &input) = 0;

        /// Starts one asynchronous pitch inference execution.
        virtual srt::Expected<void> startAsync(std::shared_ptr<const PitchStartInput> input,
                                               AsyncCallback callback) = 0;

    protected:
        using InferenceExecInstance::InferenceExecInstance;
    };

}

#endif // DSINFER_API_PITCHAPIL1_H
