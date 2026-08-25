#ifndef DSINFER_API_ACOUSTICAPIL1_H
#define DSINFER_API_ACOUSTICAPIL1_H

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <filesystem>

#include <synthrt/SVS/InferenceContrib.h>
#include <synthrt/SVS/InferenceExecInstance.h>

#include <dsinfer/Core/Tensor.h>
#include <dsinfer/Core/ParamTag.h>
#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>

namespace ds::Api::Acoustic::L1 {

    /// Identifies the acoustic inference contract.
    inline constexpr char API_INTERFACE[] = "org.openvpi.svs.AcousticInference";

    /// Identifies the ONNX implementation variant.
    inline constexpr char API_VARIANT[] = "onnx";

    /// Identifies Level 1 of the acoustic inference contract.
    inline constexpr int API_LEVEL = 1;

    using MelBase = Common::L1::MelBase;
    using MelScale = Common::L1::MelScale;
    using InputWordInfo = Common::L1::InputWordInfo;
    using InputParameterInfo = Common::L1::InputParameterInfo;
    using InputSpeakerInfo = Common::L1::InputSpeakerInfo;

    /// Describes the capabilities exported by an acoustic inference contribution.
    class AcousticSchema : public srt::ContribExports {
    public:
        inline AcousticSchema() : srt::ContribExports(API_INTERFACE, API_VARIANT, API_LEVEL) {
        }

        /// Speaker identifiers accepted by this contribution.
        std::vector<std::string> speakers;

        /// Variance parameters required as acoustic model inputs.
        std::set<ParamTag> varianceControls;

        /// Transition parameters accepted as acoustic model inputs.
        std::set<ParamTag> transitionControls;
    };

    /// Contains the interpreted configuration of an ONNX acoustic model.
    class AcousticConfiguration : public srt::ContribConfiguration {
    public:
        inline AcousticConfiguration()
            : srt::ContribConfiguration(API_INTERFACE, API_VARIANT, API_LEVEL) {
        }

        /// Maps phoneme tokens to model input identifiers.
        std::map<std::string, int> phonemes;

        /// Maps language identifiers to model input identifiers.
        std::map<std::string, int> languages;

        /// Maps speaker identifiers to their embedding vectors.
        std::map<std::string, std::vector<float>> speakers;

        /// Path of the acoustic model.
        std::filesystem::path model;

        /// Indicates whether the model consumes language identifiers.
        bool useLanguageId = false;

        /// Indicates whether the model consumes speaker embeddings.
        bool useSpeakerEmbedding = false;

        /// Width of each speaker embedding vector.
        int hiddenSize = 256;

        /// Parameters consumed by the acoustic model.
        std::set<ParamTag> parameters;

        /// Selects continuous sampling acceleration instead of discrete acceleration.
        bool useContinuousAcceleration = false;

        /// Indicates whether callers may select a sampling depth.
        bool useVariableDepth = false;

        /// Maximum sampling depth accepted by the model.
        double maxDepth = 0.0;

        /// Audio sample rate in hertz.
        int sampleRate = 44100;

        /// Number of audio samples between adjacent mel frames.
        int hopSize = 2048;

        /// Analysis window size in audio samples.
        int winSize = 2048;

        /// FFT size used to construct the mel spectrogram.
        int fftSize = 128;

        /// Number of mel channels produced by the model.
        int melChannels = 128;

        /// Lower mel filter frequency in hertz.
        int melMinFreq = 0;

        /// Upper mel filter frequency in hertz.
        int melMaxFreq = 0;

        /// Logarithmic base used by the mel transform.
        MelBase melBase = MelBase::E;

        /// Frequency scale used by the mel transform.
        MelScale melScale = MelScale::Slaney;
    };

    /// Configures one import of an acoustic inference contribution.
    class AcousticImportOptions : public srt::ContribImportOptions {
    public:
        inline AcousticImportOptions()
            : srt::ContribImportOptions(API_INTERFACE, API_VARIANT, API_LEVEL) {
        }

        /// Maps singer level speaker identifiers to contribution level identifiers.
        std::map<std::string, std::string> speakerMapping;
    };

    /// Contains runtime options used when creating an acoustic inference instance.
    class AcousticRuntimeOptions : public srt::InferenceRuntimeOptions {
    public:
        inline AcousticRuntimeOptions()
            : srt::InferenceRuntimeOptions(API_INTERFACE, API_VARIANT, API_LEVEL) {
        }
    };

    /// Contains arguments used to initialize an acoustic inference instance.
    class AcousticInitArgs : public srt::InferenceInitArgs {
    public:
        inline AcousticInitArgs() : InferenceInitArgs(API_INTERFACE, API_LEVEL) {
        }
    };

    /// Supplies score, control curves, and sampling parameters for acoustic inference.
    class AcousticStartInput : public srt::TaskStartInput {
    public:
        inline AcousticStartInput() : srt::TaskStartInput(API_INTERFACE, API_LEVEL) {
        }

        /// Total input duration in seconds.
        double duration = 0;

        /// Words and notes that define the score.
        std::vector<InputWordInfo> words;

        /// Parameter curves consumed by the acoustic model.
        std::vector<InputParameterInfo> parameters;

        /// Speaker mixture curves consumed by the acoustic model.
        std::vector<InputSpeakerInfo> speakers;

        /// Sampling depth requested from a variable depth model.
        float depth = 0;

        /// Number of sampling steps used by continuous acceleration.
        int64_t steps = 0;
    };

    /// Contains acoustic features produced by the model.
    class AcousticResult : public srt::TaskResult {
    public:
        inline AcousticResult() : srt::TaskResult(API_INTERFACE, API_LEVEL) {
        }

        /// Mel spectrogram tensor with shape <tt>(1, frames, melChannels)</tt>.
        std::shared_ptr<ITensor> mel;

        /// Fundamental frequency tensor aligned with \c mel.
        std::shared_ptr<ITensor> f0;
    };

    /// Executes one acoustic model using Level 1 typed payloads.
    ///
    /// An interpreter implementing this contract must create instances derived from this class.
    class AcousticExecInstance : public srt::InferenceExecInstance {
    public:
        using AsyncCallback =
            std::function<void(srt::Expected<std::unique_ptr<AcousticResult>> result)>;

        /// Initializes the acoustic model instance.
        virtual srt::Expected<void> initialize(const AcousticInitArgs &args) = 0;

        /// Executes acoustic inference synchronously.
        virtual srt::Expected<std::unique_ptr<AcousticResult>>
            start(const AcousticStartInput &input) = 0;

        /// Starts one asynchronous acoustic inference execution.
        virtual srt::Expected<void> startAsync(std::shared_ptr<const AcousticStartInput> input,
                                               AsyncCallback callback) = 0;

    protected:
        using InferenceExecInstance::InferenceExecInstance;
    };

}

#endif // DSINFER_API_ACOUSTICAPIL1_H
