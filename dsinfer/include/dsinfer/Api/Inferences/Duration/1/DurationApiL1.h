#ifndef DSINFER_API_INFERENCES_DURATION_1_DURATIONAPIL1_H
#define DSINFER_API_INFERENCES_DURATION_1_DURATIONAPIL1_H

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <map>

#include <synthrt/Core/ContribContract.h>
#include <synthrt/SVS/InferenceContrib.h>
#include <synthrt/SVS/InferenceExecutive.h>

#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>

namespace ds::Api::Duration::L1 {

    /// Identifies the Duration interface contract.
    inline const srt::ContribContract Contract{"org.openvpi.dsinfer.inference.Duration", 1};

    using InputWordInfo = Common::L1::InputWordInfo;

    /// Describes the capabilities exported by a duration inference contribution.
    class Schema : public srt::ContribExports {
    public:
        inline Schema() : srt::ContribExports(Contract) {
        }

        /// Speaker identifiers accepted by this contribution.
        std::vector<std::string> speakers;
    };

    /// Configures one import of a duration inference contribution.
    class ImportOptions : public srt::ContribImportOptions {
    public:
        inline ImportOptions() : srt::ContribImportOptions(Contract) {
        }

        /// Maps singer level speaker identifiers to contribution level identifiers.
        std::map<std::string, std::string> speakerMapping;
    };

    /// Contains runtime options used when creating a duration inference executive.
    class RuntimeOptions : public srt::InferenceRuntimeOptions {
    public:
        inline RuntimeOptions() : srt::InferenceRuntimeOptions(Contract) {
        }
    };

    /// Contains arguments used to initialize a duration inference executive.
    class InitArgs : public srt::InferenceInitArgs {
    public:
        inline InitArgs() : InferenceInitArgs(Contract.interfaceId(), Contract.level()) {
        }
    };

    /// Supplies the score and timing context for duration prediction.
    class StartInput : public srt::TaskStartInput {
    public:
        inline StartInput() : srt::TaskStartInput(Contract.interfaceId(), Contract.level()) {
        }

        /// Total input duration in seconds.
        double duration = 0;

        /// Words whose phoneme durations will be predicted.
        std::vector<InputWordInfo> words;
    };

    /// Contains predicted phoneme durations.
    class Result : public srt::TaskResult {
    public:
        inline Result() : srt::TaskResult(Contract.interfaceId(), Contract.level()) {
        }

        /// Predicted duration of each input phoneme in seconds.
        std::vector<double> durations;
    };

    /// Executes one duration model using Level 1 typed payloads.
    ///
    /// An interpreter implementing this contract must create executives derived from this class.
    class Executive : public srt::InferenceExecutive {
    public:
        using AsyncCallback = std::function<void(srt::Expected<std::unique_ptr<Result>> result)>;

        /// Initializes the duration model instance.
        virtual srt::Expected<void> initialize(const InitArgs &args) = 0;

        /// Executes duration inference synchronously.
        virtual srt::Expected<std::unique_ptr<Result>> start(const StartInput &input) = 0;

        /// Starts one asynchronous duration inference execution.
        virtual srt::Expected<void> startAsync(std::shared_ptr<const StartInput> input,
                                               AsyncCallback callback) = 0;

    protected:
        using InferenceExecutive::InferenceExecutive;
    };

}

#endif // DSINFER_API_INFERENCES_DURATION_1_DURATIONAPIL1_H
