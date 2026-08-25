#ifndef DSINFER_API_DURATIONAPIL1_H
#define DSINFER_API_DURATIONAPIL1_H

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <filesystem>

#include <synthrt/SVS/InferenceContrib.h>
#include <synthrt/SVS/InferenceExecInstance.h>

#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>

namespace ds::Api::Duration::L1 {

    /// Identifies the duration inference contract.
    inline constexpr char API_INTERFACE[] = "org.openvpi.svs.DurationInference";

    /// Identifies the ONNX implementation variant.
    inline constexpr char API_VARIANT[] = "onnx";

    /// Identifies Level 1 of the duration inference contract.
    inline constexpr int API_LEVEL = 1;

    using InputWordInfo = Common::L1::InputWordInfo;

    /// Describes the capabilities exported by a duration inference contribution.
    class DurationSchema : public srt::ContribExports {
    public:
        inline DurationSchema() : srt::ContribExports(API_INTERFACE, API_VARIANT, API_LEVEL) {
        }

        /// Speaker identifiers accepted by this contribution.
        std::vector<std::string> speakers;
    };

    /// Contains the interpreted configuration of an ONNX duration model.
    class DurationConfiguration : public srt::ContribConfiguration {
    public:
        inline DurationConfiguration()
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

        /// Path of the duration predictor model.
        std::filesystem::path predictor;

        /// Duration of one model frame in seconds.
        double frameWidth = 512.0 / 44100.0;

        /// Indicates whether the encoder consumes language identifiers.
        bool useLanguageId = false;

        /// Indicates whether the predictor consumes speaker embeddings.
        bool useSpeakerEmbedding = false;

        /// Width of the encoder state and each speaker embedding vector.
        int hiddenSize = 256;
    };

    /// Configures one import of a duration inference contribution.
    class DurationImportOptions : public srt::ContribImportOptions {
    public:
        inline DurationImportOptions()
            : srt::ContribImportOptions(API_INTERFACE, API_VARIANT, API_LEVEL) {
        }

        /// Maps singer level speaker identifiers to contribution level identifiers.
        std::map<std::string, std::string> speakerMapping;
    };

    /// Contains runtime options used when creating a duration inference instance.
    class DurationRuntimeOptions : public srt::InferenceRuntimeOptions {
    public:
        inline DurationRuntimeOptions()
            : srt::InferenceRuntimeOptions(API_INTERFACE, API_VARIANT, API_LEVEL) {
        }
    };

    /// Contains arguments used to initialize a duration inference instance.
    class DurationInitArgs : public srt::InferenceInitArgs {
    public:
        inline DurationInitArgs() : InferenceInitArgs(API_INTERFACE, API_LEVEL) {
        }
    };

    /// Supplies the score and timing context for duration prediction.
    class DurationStartInput : public srt::TaskStartInput {
    public:
        inline DurationStartInput() : srt::TaskStartInput(API_INTERFACE, API_LEVEL) {
        }

        /// Total input duration in seconds.
        double duration = 0;

        /// Words whose phoneme durations will be predicted.
        std::vector<InputWordInfo> words;
    };

    /// Contains predicted phoneme durations.
    class DurationResult : public srt::TaskResult {
    public:
        inline DurationResult() : srt::TaskResult(API_INTERFACE, API_LEVEL) {
        }

        /// Predicted duration of each input phoneme in seconds.
        std::vector<double> durations;
    };

    /// Executes one duration model using Level 1 typed payloads.
    ///
    /// An interpreter implementing this contract must create instances derived from this class.
    class DurationExecInstance : public srt::InferenceExecInstance {
    public:
        using AsyncCallback =
            std::function<void(srt::Expected<std::unique_ptr<DurationResult>> result)>;

        /// Initializes the duration model instance.
        virtual srt::Expected<void> initialize(const DurationInitArgs &args) = 0;

        /// Executes duration inference synchronously.
        virtual srt::Expected<std::unique_ptr<DurationResult>>
            start(const DurationStartInput &input) = 0;

        /// Starts one asynchronous duration inference execution.
        virtual srt::Expected<void> startAsync(std::shared_ptr<const DurationStartInput> input,
                                               AsyncCallback callback) = 0;

    protected:
        using InferenceExecInstance::InferenceExecInstance;
    };

}

#endif // DSINFER_API_DURATIONAPIL1_H
