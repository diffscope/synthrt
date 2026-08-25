#ifndef DSINFER_API_VOCODERAPIL1_H
#define DSINFER_API_VOCODERAPIL1_H

#include <functional>
#include <memory>

#include <synthrt/SVS/InferenceContrib.h>
#include <synthrt/SVS/InferenceExecutive.h>

#include <dsinfer/Core/Tensor.h>
#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>

namespace ds::Api::Vocoder::L1 {

    /// Identifies the vocoder inference contract.
    inline constexpr char API_INTERFACE[] = "org.openvpi.dsinfer.inference.Vocoder";

    /// Identifies the ONNX implementation variant.
    inline constexpr char API_VARIANT[] = "onnx";

    /// Identifies Level 1 of the vocoder inference contract.
    inline constexpr int API_LEVEL = 1;

    using MelBase = Common::L1::MelBase;

    using MelScale = Common::L1::MelScale;

    /// Configures one import of a vocoder inference contribution.
    class VocoderImportOptions : public srt::ContribImportOptions {
    public:
        inline VocoderImportOptions()
            : srt::ContribImportOptions(API_INTERFACE, API_VARIANT, API_LEVEL) {
        }
    };

    /// Contains runtime options used when creating a vocoder inference executive.
    class VocoderRuntimeOptions : public srt::InferenceRuntimeOptions {
    public:
        inline VocoderRuntimeOptions()
            : srt::InferenceRuntimeOptions(API_INTERFACE, API_VARIANT, API_LEVEL) {
        }
    };

    /// Describes the capabilities exported by a vocoder inference contribution.
    class VocoderSchema : public srt::ContribExports {
    public:
        inline VocoderSchema() : srt::ContribExports(API_INTERFACE, API_VARIANT, API_LEVEL) {
        }
    };

    /// Contains the interpreted configuration of an ONNX vocoder model.
    class VocoderConfiguration : public srt::ContribConfiguration {
    public:
        inline VocoderConfiguration()
            : srt::ContribConfiguration(API_INTERFACE, API_VARIANT, API_LEVEL) {
        }

        /// Path of the vocoder model.
        std::filesystem::path model;

        /// Audio sample rate in hertz.
        int sampleRate = 44100;

        /// Number of audio samples between adjacent mel frames.
        int hopSize = 2048;

        /// Analysis window size in audio samples.
        int winSize = 2048;

        /// FFT size used to construct the input mel spectrogram.
        int fftSize = 128;

        /// Number of channels in the input mel spectrogram.
        int melChannels = 128;

        /// Lower mel filter frequency in hertz.
        int melMinFreq = 0;

        /// Upper mel filter frequency in hertz.
        int melMaxFreq = 0;

        /// Logarithmic base used by the mel transform.
        MelBase melBase = MelBase::E;

        /// Frequency scale used by the mel transform.
        MelScale melScale = MelScale::Slaney;

        /// Indicates whether the vocoder accepts an independently modified pitch curve.
        bool pitchControllable = false;
    };

    /// Contains arguments used to initialize a vocoder inference executive.
    class VocoderInitArgs : public srt::InferenceInitArgs {
    public:
        inline VocoderInitArgs() : InferenceInitArgs(API_INTERFACE, API_LEVEL) {
        }
    };

    /// Supplies acoustic features to a vocoder inference task.
    class VocoderStartInput : public srt::TaskStartInput {
    public:
        inline VocoderStartInput() : srt::TaskStartInput(API_INTERFACE, API_LEVEL) {
        }

        /// Mel spectrogram tensor with shape <tt>(1, frames, melChannels)</tt>.
        std::shared_ptr<ITensor> mel;

        /// Fundamental frequency tensor aligned with \c mel.
        std::shared_ptr<ITensor> f0;
    };

    /// Contains waveform samples produced by the vocoder.
    class VocoderResult : public srt::TaskResult {
    public:
        inline VocoderResult() : srt::TaskResult(API_INTERFACE, API_LEVEL) {
        }

        /// Native byte representation of contiguous 32 bit floating point waveform samples.
        std::vector<uint8_t> audioData;
    };

    /// Executes one vocoder model using Level 1 typed payloads.
    ///
    /// An interpreter implementing this contract must create executives derived from this class.
    class VocoderExecutive : public srt::InferenceExecutive {
    public:
        using AsyncCallback =
            std::function<void(srt::Expected<std::unique_ptr<VocoderResult>> result)>;

        /// Initializes the vocoder model instance.
        virtual srt::Expected<void> initialize(const VocoderInitArgs &args) = 0;

        /// Executes vocoder inference synchronously.
        virtual srt::Expected<std::unique_ptr<VocoderResult>>
            start(const VocoderStartInput &input) = 0;

        /// Starts one asynchronous vocoder inference execution.
        virtual srt::Expected<void> startAsync(std::shared_ptr<const VocoderStartInput> input,
                                               AsyncCallback callback) = 0;

    protected:
        using InferenceExecutive::InferenceExecutive;
    };

}

#endif // DSINFER_API_VOCODERAPIL1_H
