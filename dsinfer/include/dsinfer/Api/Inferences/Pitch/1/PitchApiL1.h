#ifndef DSINFER_API_INFERENCES_PITCH_1_PITCHAPIL1_H
#define DSINFER_API_INFERENCES_PITCH_1_PITCHAPIL1_H

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <map>

#include <synthrt/Core/ContribContract.h>
#include <synthrt/SVS/InferenceContrib.h>
#include <synthrt/SVS/InferenceExecutive.h>

#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>

namespace ds::Api::Pitch::L1 {

    /// Identifies the Pitch interface contract.
    inline const srt::ContribContract Contract{"org.openvpi.dsinfer.inference.Pitch", 1};

    using LinguisticMode = Common::L1::LinguisticMode;
    using InputWordInfo = Common::L1::InputWordInfo;
    using InputParameterInfo = Common::L1::InputParameterInfo;
    using InputSpeakerInfo = Common::L1::InputSpeakerInfo;

    /// Describes the capabilities exported by a pitch inference contribution.
    class Exports : public srt::ContribExports {
    public:
        inline Exports() : srt::ContribExports(Contract) {
        }

        /// Speaker identifiers accepted by this contribution.
        std::vector<std::string> speakers;

        /// Indicates whether the contribution accepts an expressiveness curve.
        bool allowExpressiveness = true;
    };

    /// Configures one import of a pitch inference contribution.
    class ImportOptions : public srt::ContribImportOptions {
    public:
        inline ImportOptions() : srt::ContribImportOptions(Contract) {
        }

        /// Maps singer level speaker identifiers to contribution level identifiers.
        std::map<std::string, std::string> speakerMapping;
    };

    /// Contains runtime options used when creating a pitch inference executive.
    class RuntimeOptions : public srt::InferenceRuntimeOptions {
    public:
        inline RuntimeOptions() : srt::InferenceRuntimeOptions(Contract) {
        }
    };

    /// Contains arguments used to initialize a pitch inference executive.
    class InitArgs : public srt::InferenceInitArgs {
    public:
        inline InitArgs() : InferenceInitArgs(Contract) {
        }
    };

    /// Supplies score, control curves, and sampling parameters for pitch prediction.
    class StartInput : public srt::TaskStartInput {
    public:
        inline StartInput() : srt::TaskStartInput(Contract) {
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
    class Result : public srt::TaskResult {
    public:
        inline Result() : srt::TaskResult(Contract) {
        }

        /// Predicted pitch samples in semitones.
        std::vector<double> pitch;

        /// Time between adjacent pitch samples in seconds.
        double interval = 0;
    };

    /// Executes one pitch model using Level 1 typed payloads.
    ///
    /// An interpreter implementing this contract must create executives derived from this class.
    class Executive : public srt::InferenceExecutive {
    public:
        using AsyncCallback = std::function<void(srt::Expected<std::unique_ptr<Result>> result)>;

        /// Initializes the pitch model instance.
        virtual srt::Expected<void> initialize(const InitArgs &args) = 0;

        /// Executes pitch inference synchronously.
        virtual srt::Expected<std::unique_ptr<Result>> start(const StartInput &input) = 0;

        /// Starts one asynchronous pitch inference execution.
        virtual srt::Expected<void> startAsync(std::shared_ptr<const StartInput> input,
                                               AsyncCallback callback) = 0;

    protected:
        using InferenceExecutive::InferenceExecutive;
    };

}

#endif // DSINFER_API_INFERENCES_PITCH_1_PITCHAPIL1_H
