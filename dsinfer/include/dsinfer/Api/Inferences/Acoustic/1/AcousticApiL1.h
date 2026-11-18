#ifndef DSINFER_API_INFERENCES_ACOUSTIC_1_ACOUSTICAPIL1_H
#define DSINFER_API_INFERENCES_ACOUSTIC_1_ACOUSTICAPIL1_H

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <set>

#include <synthrt/Core/ContribContract.h>
#include <synthrt/SVS/InferenceContrib.h>
#include <synthrt/SVS/InferenceExecutive.h>

#include <dsinfer/Core/Tensor.h>
#include <dsinfer/Core/ParamTag.h>
#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>

namespace ds::Api::Acoustic::L1 {

    /// Identifies the Acoustic interface contract.
    inline const srt::ContribContract Contract{"org.openvpi.dsinfer.inference.Acoustic", 1};

    using MelBase = Common::L1::MelBase;
    using MelScale = Common::L1::MelScale;
    using InputWordInfo = Common::L1::InputWordInfo;
    using InputParameterInfo = Common::L1::InputParameterInfo;
    using InputSpeakerInfo = Common::L1::InputSpeakerInfo;

    /// Describes the capabilities exported by an acoustic inference contribution.
    class Schema : public srt::ContribExports {
    public:
        inline Schema() : srt::ContribExports(Contract) {
        }

        /// Speaker identifiers accepted by this contribution.
        std::vector<std::string> speakers;

        /// Variance parameters required as acoustic model inputs.
        std::set<ParamTag> varianceControls;

        /// Transition parameters accepted as acoustic model inputs.
        std::set<ParamTag> transitionControls;
    };

    /// Configures one import of an acoustic inference contribution.
    class ImportOptions : public srt::ContribImportOptions {
    public:
        inline ImportOptions() : srt::ContribImportOptions(Contract) {
        }

        /// Maps singer level speaker identifiers to contribution level identifiers.
        std::map<std::string, std::string> speakerMapping;
    };

    /// Contains runtime options used when creating an acoustic inference executive.
    class RuntimeOptions : public srt::InferenceRuntimeOptions {
    public:
        inline RuntimeOptions() : srt::InferenceRuntimeOptions(Contract) {
        }
    };

    /// Contains arguments used to initialize an acoustic inference executive.
    class InitArgs : public srt::InferenceInitArgs {
    public:
        inline InitArgs() : InferenceInitArgs(Contract.interfaceId(), Contract.level()) {
        }
    };

    /// Supplies score, control curves, and sampling parameters for acoustic inference.
    class StartInput : public srt::TaskStartInput {
    public:
        inline StartInput() : srt::TaskStartInput(Contract.interfaceId(), Contract.level()) {
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
    class Result : public srt::TaskResult {
    public:
        inline Result() : srt::TaskResult(Contract.interfaceId(), Contract.level()) {
        }

        /// Mel spectrogram tensor with shape <tt>(1, frames, melChannels)</tt>.
        std::shared_ptr<ITensor> mel;

        /// Fundamental frequency tensor aligned with \c mel.
        std::shared_ptr<ITensor> f0;
    };

    /// Executes one acoustic model using Level 1 typed payloads.
    ///
    /// An interpreter implementing this contract must create executives derived from this class.
    class Executive : public srt::InferenceExecutive {
    public:
        using AsyncCallback = std::function<void(srt::Expected<std::unique_ptr<Result>> result)>;

        /// Initializes the acoustic model instance.
        virtual srt::Expected<void> initialize(const InitArgs &args) = 0;

        /// Executes acoustic inference synchronously.
        virtual srt::Expected<std::unique_ptr<Result>> start(const StartInput &input) = 0;

        /// Starts one asynchronous acoustic inference execution.
        virtual srt::Expected<void> startAsync(std::shared_ptr<const StartInput> input,
                                               AsyncCallback callback) = 0;

    protected:
        using InferenceExecutive::InferenceExecutive;
    };

}

#endif // DSINFER_API_INFERENCES_ACOUSTIC_1_ACOUSTICAPIL1_H
