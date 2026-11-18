#ifndef DSINFER_API_INFERENCES_VOCODER_1_VOCODERAPIL1_H
#define DSINFER_API_INFERENCES_VOCODER_1_VOCODERAPIL1_H

#include <functional>
#include <memory>

#include <synthrt/Core/ContribContract.h>
#include <synthrt/SVS/InferenceContrib.h>
#include <synthrt/SVS/InferenceExecutive.h>

#include <dsinfer/Core/Tensor.h>
#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>

namespace ds::Api::Vocoder::L1 {

    /// Identifies the Vocoder interface contract.
    inline const srt::ContribContract Contract{"org.openvpi.dsinfer.inference.Vocoder", 1};

    using MelBase = Common::L1::MelBase;

    using MelScale = Common::L1::MelScale;

    /// Configures one import of a vocoder inference contribution.
    class ImportOptions : public srt::ContribImportOptions {
    public:
        inline ImportOptions() : srt::ContribImportOptions(Contract) {
        }
    };

    /// Contains runtime options used when creating a vocoder inference executive.
    class RuntimeOptions : public srt::InferenceRuntimeOptions {
    public:
        inline RuntimeOptions() : srt::InferenceRuntimeOptions(Contract) {
        }
    };

    /// Describes the capabilities exported by a vocoder inference contribution.
    class Exports : public srt::ContribExports {
    public:
        inline Exports() : srt::ContribExports(Contract) {
        }
    };

    /// Contains arguments used to initialize a vocoder inference executive.
    class InitArgs : public srt::InferenceInitArgs {
    public:
        inline InitArgs() : InferenceInitArgs(Contract.interfaceId(), Contract.level()) {
        }
    };

    /// Supplies acoustic features to a vocoder inference task.
    class StartInput : public srt::TaskStartInput {
    public:
        inline StartInput() : srt::TaskStartInput(Contract.interfaceId(), Contract.level()) {
        }

        /// Mel spectrogram tensor with shape <tt>(1, frames, melChannels)</tt>.
        std::shared_ptr<ITensor> mel;

        /// Fundamental frequency tensor aligned with \c mel.
        std::shared_ptr<ITensor> f0;
    };

    /// Contains waveform samples produced by the vocoder.
    class Result : public srt::TaskResult {
    public:
        inline Result() : srt::TaskResult(Contract.interfaceId(), Contract.level()) {
        }

        /// Native byte representation of contiguous 32 bit floating point waveform samples.
        std::vector<uint8_t> audioData;
    };

    /// Executes one vocoder model using Level 1 typed payloads.
    ///
    /// An interpreter implementing this contract must create executives derived from this class.
    class Executive : public srt::InferenceExecutive {
    public:
        using AsyncCallback = std::function<void(srt::Expected<std::unique_ptr<Result>> result)>;

        /// Initializes the vocoder model instance.
        virtual srt::Expected<void> initialize(const InitArgs &args) = 0;

        /// Executes vocoder inference synchronously.
        virtual srt::Expected<std::unique_ptr<Result>> start(const StartInput &input) = 0;

        /// Starts one asynchronous vocoder inference execution.
        virtual srt::Expected<void> startAsync(std::shared_ptr<const StartInput> input,
                                               AsyncCallback callback) = 0;

    protected:
        using InferenceExecutive::InferenceExecutive;
    };

}

#endif // DSINFER_API_INFERENCES_VOCODER_1_VOCODERAPIL1_H
