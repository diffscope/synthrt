#ifndef DSINFER_API_INFERENCES_VARIANCE_1_VARIANCEAPIL1_H
#define DSINFER_API_INFERENCES_VARIANCE_1_VARIANCEAPIL1_H

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <set>

#include <synthrt/Core/ContribContract.h>
#include <synthrt/SVS/InferenceContrib.h>
#include <synthrt/SVS/InferenceExecutive.h>

#include <dsinfer/Core/ParamTag.h>
#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>

namespace ds::Api::Variance::L1 {

    /// Identifies the Variance interface contract.
    inline const srt::ContribContract Contract{"org.openvpi.dsinfer.inference.Variance", 1};

    using InputWordInfo = Common::L1::InputWordInfo;
    using InputParameterInfo = Common::L1::InputParameterInfo;
    using InputSpeakerInfo = Common::L1::InputSpeakerInfo;
    using LinguisticMode = Common::L1::LinguisticMode;

    /// Describes the capabilities exported by a variance inference contribution.
    class Schema : public srt::ContribExports {
    public:
        inline Schema() : srt::ContribExports(Contract) {
        }

        /// Speaker identifiers accepted by this contribution.
        std::vector<std::string> speakers;

        /// Parameters predicted by the model in model output order.
        std::vector<ParamTag> predictions;
    };



    /// Configures one import of a variance inference contribution.
    class ImportOptions : public srt::ContribImportOptions {
    public:
        inline ImportOptions() : srt::ContribImportOptions(Contract) {
        }

        /// Maps singer level speaker identifiers to contribution level identifiers.
        std::map<std::string, std::string> speakerMapping;

        /// Selects the exported parameters consumed by the importer.
        std::set<ParamTag> predictions;
    };

    /// Contains runtime options used when creating a variance inference executive.
    class RuntimeOptions : public srt::InferenceRuntimeOptions {
    public:
        inline RuntimeOptions() : srt::InferenceRuntimeOptions(Contract) {
        }
    };

    /// Contains arguments used to initialize a variance inference executive.
    class InitArgs : public srt::InferenceInitArgs {
    public:
        inline InitArgs() : InferenceInitArgs(Contract.interfaceId(), Contract.level()) {
        }
    };

    /// Supplies score, control curves, and sampling parameters for variance prediction.
    class StartInput : public srt::TaskStartInput {
    public:
        inline StartInput() : srt::TaskStartInput(Contract.interfaceId(), Contract.level()) {
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
    class Result : public srt::TaskResult {
    public:
        using InputParameterInfo = Common::L1::InputParameterInfo;

        inline Result() : srt::TaskResult(Contract.interfaceId(), Contract.level()) {
        }

        /// Predicted parameter curves in the order declared by the contribution.
        std::vector<InputParameterInfo> predictions;
    };

    /// Executes one variance model using Level 1 typed payloads.
    ///
    /// An interpreter implementing this contract must create executives derived from this class.
    class Executive : public srt::InferenceExecutive {
    public:
        using AsyncCallback = std::function<void(srt::Expected<std::unique_ptr<Result>> result)>;

        /// Initializes the variance model instance.
        virtual srt::Expected<void> initialize(const InitArgs &args) = 0;

        /// Executes variance inference synchronously.
        virtual srt::Expected<std::unique_ptr<Result>> start(const StartInput &input) = 0;

        /// Starts one asynchronous variance inference execution.
        virtual srt::Expected<void> startAsync(std::shared_ptr<const StartInput> input,
                                               AsyncCallback callback) = 0;

    protected:
        using InferenceExecutive::InferenceExecutive;
    };

}

#endif // DSINFER_API_INFERENCES_VARIANCE_1_VARIANCEAPIL1_H
