#include <atomic>
#include <cstring>
#include <vector>

#include <dsinfer/Support/ErrorCode.h>

#include <memory>
#include <string>

#include <duration/DurationInterpreter.h>
#include <duration/DurationInference.h>
#include <duration/DurationTask.h>

namespace Contract = ds::Api::Duration::L1;
using TestInterpreter = ds::DurationInterpreter;

#define BOOST_TEST_MAIN
#include "../TestSupport.h"
#include "../RecordingDriver.h"

namespace {
    class OtherInit : public srt::TaskInitArgs {
    public:
        OtherInit() : TaskInitArgs("com.example.Other", 1) {
        }
    };
    class OtherInput : public srt::TaskStartInput {
    public:
        OtherInput() : TaskStartInput("com.example.Other", 1) {
        }
    };
}

BOOST_AUTO_TEST_CASE(task_rejects_invalid_initialization_and_uninitialized_execution) {
    ds::test::DeclaredInference declaration;
    ds::DurationInference executive(declaration.spec());
    ds::DurationTask task(executive);
    OtherInit args;
    OtherInput input;
    auto initialized = task.initialize(static_cast<const srt::TaskInitArgs &>(args));
    BOOST_REQUIRE(!initialized);
    BOOST_CHECK(initialized.error().code() == srt::Error::InvalidArgument);
    auto result = task.start(static_cast<const srt::TaskStartInput &>(input));
    BOOST_REQUIRE(!result);
    BOOST_CHECK(result.error().code() == ds::ErrorCode::NotInitialized);
    bool called = false;
    auto async = task.startAsync(std::shared_ptr<const srt::TaskStartInput>(),
                                 [&called](auto) { called = true; });
    BOOST_REQUIRE(!async);
    BOOST_CHECK(async.error().code() == srt::Error::InvalidArgument);
    BOOST_CHECK(!called);
    BOOST_REQUIRE(task.waitForFinished());
}

namespace {
    Contract::StartInput makeInput() {
        Contract::StartInput input;
        input.duration = 0.04;
        ds::Api::Common::L1::InputWordInfo word;
        word.phones.push_back({"a", "", 0, 0});
        word.notes.push_back({69, 0, 0.04});
        input.words.push_back(word);

        return input;
    }

    srt::JsonObject executionConfiguration() {
        return srt::JsonObject{
            {"useContinuousAcceleration", false}
        };
    }
}

BOOST_AUTO_TEST_CASE(modelInputsResultsAndReuse) {
    auto trace = std::make_shared<ds::test::DriverTrace>();
    ds::test::DeclaredInference declaration(std::make_unique<ds::test::RecordingDriver>(trace),
                                            executionConfiguration());
    ds::DurationInference executive(declaration.spec());
    ds::DurationTask task(executive);
    BOOST_REQUIRE(task.initialize(Contract::InitArgs()));
    auto input = makeInput();
    auto result = task.start(input);
    BOOST_REQUIRE_MESSAGE(result, (result ? std::string() : result.error().message()));
    BOOST_CHECK(task.state() == srt::ITask::Succeeded);
    BOOST_REQUIRE_EQUAL(result.value()->durations.size(), 1u);
    BOOST_CHECK_CLOSE(result.value()->durations[0], 0.04, 0.0001);
    BOOST_REQUIRE_EQUAL(trace->calls.size(), 2u);
    BOOST_CHECK_EQUAL(trace->calls.front().inputs.at("tokens")->data<int64_t>()[0], 1);
    BOOST_CHECK_EQUAL(trace->calls.back().outputs.count("ph_dur_pred"), 1u);
    auto repeated = task.start(input);
    BOOST_REQUIRE(repeated);
    BOOST_CHECK(task.state() == srt::ITask::Succeeded);
}

BOOST_AUTO_TEST_CASE(openFailuresAreReportedAndInitializationCanBeRetried) {
    for (const auto model : {"encoder.onnx", "predictor.onnx"}) {
        auto trace = std::make_shared<ds::test::DriverTrace>();
        trace->fault = ds::test::SessionFault::Open;
        trace->faultModel = model;
        ds::test::DeclaredInference declaration(std::make_unique<ds::test::RecordingDriver>(trace),
                                                executionConfiguration());
        ds::DurationInference executive(declaration.spec());
        {
            ds::DurationTask task(executive);
            auto result = task.initialize(Contract::InitArgs());
            BOOST_REQUIRE(!result);
            BOOST_CHECK(result.error().code() == srt::Error::FileNotOpen);
            BOOST_CHECK(task.state() == srt::ITask::Failed);
            trace->fault = ds::test::SessionFault::None;
            BOOST_REQUIRE(task.initialize(Contract::InitArgs()));
            BOOST_CHECK(task.state() == srt::ITask::Idle);
        }
        BOOST_CHECK_EQUAL(trace->destroyed.load(), trace->created.load());
    }
}

BOOST_AUTO_TEST_CASE(modelFailuresAndMalformedResultsAreRejected) {
    for (const auto fault :
         {ds::test::SessionFault::Run, ds::test::SessionFault::NullResult,
          ds::test::SessionFault::WrongType, ds::test::SessionFault::MissingOutput}) {
        auto trace = std::make_shared<ds::test::DriverTrace>();
        trace->fault = fault;
        trace->faultModel = "predictor.onnx";
        ds::test::DeclaredInference declaration(std::make_unique<ds::test::RecordingDriver>(trace),
                                                executionConfiguration());
        ds::DurationInference executive(declaration.spec());
        ds::DurationTask task(executive);
        BOOST_REQUIRE(task.initialize(Contract::InitArgs()));
        auto input = makeInput();
        auto result = task.start(input);
        BOOST_REQUIRE(!result);
        BOOST_CHECK(!result.error().message().empty());
        BOOST_CHECK(task.state() == srt::ITask::Failed);
        trace->fault = ds::test::SessionFault::None;
        BOOST_REQUIRE(task.start(input));
        BOOST_CHECK(task.state() == srt::ITask::Succeeded);
    }
}

BOOST_AUTO_TEST_CASE(typedAsyncCallbackRunsOnceAndCompletesBeforeWaitReturns) {
    auto trace = std::make_shared<ds::test::DriverTrace>();
    ds::test::DeclaredInference declaration(std::make_unique<ds::test::RecordingDriver>(trace),
                                            executionConfiguration());
    ds::DurationInference executive(declaration.spec());
    ds::DurationTask task(executive);
    BOOST_REQUIRE(task.initialize(Contract::InitArgs()));
    auto input = std::make_shared<Contract::StartInput>(makeInput());
    std::atomic<int> callbacks{0};
    bool succeeded = false;
    BOOST_REQUIRE(
        task.startAsync(input, [&](srt::Expected<std::unique_ptr<Contract::Result>> result) {
            succeeded = result && result.value() != nullptr;
            ++callbacks;
        }));
    BOOST_REQUIRE(task.waitForFinished());
    BOOST_CHECK(succeeded);
    BOOST_CHECK_EQUAL(callbacks.load(), 1);
    BOOST_CHECK(task.state() == srt::ITask::Succeeded);
    BOOST_CHECK_GT(trace->waited.load(), 0);
}

BOOST_AUTO_TEST_CASE(stopInterruptsAModelAndDoesNotCancelTheNextExecution) {
    auto trace = std::make_shared<ds::test::DriverTrace>();
    trace->block = true;
    ds::test::DeclaredInference declaration(std::make_unique<ds::test::RecordingDriver>(trace),
                                            executionConfiguration());
    ds::DurationInference executive(declaration.spec());
    ds::DurationTask task(executive);
    BOOST_REQUIRE(task.initialize(Contract::InitArgs()));
    auto input = std::make_shared<Contract::StartInput>(makeInput());
    std::atomic<int> callbacks{0};
    BOOST_REQUIRE(task.startAsync(
        input, [&](srt::Expected<std::unique_ptr<Contract::Result>>) { ++callbacks; }));
    BOOST_REQUIRE(trace->waitUntilEntered());
    auto duplicate =
        task.startAsync(input, [](srt::Expected<std::unique_ptr<Contract::Result>>) {});
    BOOST_REQUIRE(!duplicate);
    BOOST_CHECK(duplicate.error().code() == srt::Error::InvalidArgument);
    BOOST_REQUIRE(task.stop());
    BOOST_REQUIRE(task.waitForFinished());
    BOOST_CHECK_GT(trace->stopped.load(), 0);
    BOOST_CHECK_EQUAL(callbacks.load(), 1);
    BOOST_CHECK(task.state() == srt::ITask::Canceled);
    trace->block = false;
    BOOST_REQUIRE(task.start(*input));
    BOOST_CHECK(task.state() == srt::ITask::Succeeded);
}

BOOST_AUTO_TEST_CASE(destructionStopsAndJoinsAnActiveExecution) {
    auto trace = std::make_shared<ds::test::DriverTrace>();
    trace->block = true;
    ds::test::DeclaredInference declaration(std::make_unique<ds::test::RecordingDriver>(trace),
                                            executionConfiguration());
    ds::DurationInference executive(declaration.spec());
    auto task = std::make_unique<ds::DurationTask>(executive);
    BOOST_REQUIRE(task->initialize(Contract::InitArgs()));
    auto input = std::make_shared<Contract::StartInput>(makeInput());
    std::atomic<int> callbacks{0};
    BOOST_REQUIRE(task->startAsync(
        input, [&](srt::Expected<std::unique_ptr<Contract::Result>>) { ++callbacks; }));
    BOOST_REQUIRE(trace->waitUntilEntered());
    task.reset();
    BOOST_CHECK_EQUAL(callbacks.load(), 1);
    BOOST_CHECK_GT(trace->stopped.load(), 0);
    BOOST_CHECK_EQUAL(trace->destroyed.load(), trace->created.load());
}

BOOST_AUTO_TEST_CASE(incompatibleResultVersionsAreRejectedBeforeCasting) {
    auto trace = std::make_shared<ds::test::DriverTrace>();
    trace->fault = ds::test::SessionFault::WrongVersion;
    trace->faultModel = "predictor.onnx";
    ds::test::DeclaredInference declaration(std::make_unique<ds::test::RecordingDriver>(trace),
                                            executionConfiguration());
    ds::DurationInference executive(declaration.spec());
    ds::DurationTask task(executive);
    BOOST_REQUIRE(task.initialize(Contract::InitArgs()));
    auto input = makeInput();
    auto result = task.start(input);
    BOOST_REQUIRE(!result);
    BOOST_CHECK(result.error().code() == srt::Error::InvalidArgument);
    BOOST_CHECK(task.state() == srt::ITask::Failed);
}
namespace {
    class UnsupportedInit : public srt::TaskInitArgs {
    public:
        UnsupportedInit()
            : TaskInitArgs(Contract::Contract.interfaceId(), Contract::Contract.level() + 1) {
        }
    };
    class UnsupportedInput : public srt::TaskStartInput {
    public:
        UnsupportedInput()
            : TaskStartInput(Contract::Contract.interfaceId(), Contract::Contract.level() + 1) {
        }
    };
}

BOOST_AUTO_TEST_CASE(initializedTasksRefuseUnsupportedInputVersions) {
    auto trace = std::make_shared<ds::test::DriverTrace>();
    ds::test::DeclaredInference declaration(std::make_unique<ds::test::RecordingDriver>(trace),
                                            executionConfiguration());
    ds::DurationInference executive(declaration.spec());
    ds::DurationTask task(executive);
    BOOST_REQUIRE(task.initialize(Contract::InitArgs()));
    UnsupportedInit init;
    auto initialization = task.initialize(static_cast<const srt::TaskInitArgs &>(init));
    BOOST_REQUIRE(!initialization);
    BOOST_CHECK(initialization.error().code() == srt::Error::InvalidArgument);
    UnsupportedInput input;
    auto started = task.start(static_cast<const srt::TaskStartInput &>(input));
    BOOST_REQUIRE(!started);
    BOOST_CHECK(started.error().code() == srt::Error::InvalidArgument);
    bool called = false;
    auto asynchronous = task.startAsync(std::make_shared<const UnsupportedInput>(),
                                        srt::ITask::AsyncCallback([&](auto) { called = true; }));
    BOOST_REQUIRE(!asynchronous);
    BOOST_CHECK(asynchronous.error().code() == srt::Error::InvalidArgument);
    BOOST_CHECK(!called);
    BOOST_CHECK(trace->calls.empty());
}
BOOST_AUTO_TEST_CASE(unknownTokensAndShortPredictionsAreReported) {
    auto trace = std::make_shared<ds::test::DriverTrace>();
    ds::test::DeclaredInference declaration(std::make_unique<ds::test::RecordingDriver>(trace),
                                            executionConfiguration());
    ds::DurationInference executive(declaration.spec());
    ds::DurationTask task(executive);
    BOOST_REQUIRE(task.initialize(Contract::InitArgs()));
    auto input = makeInput();
    input.words[0].phones[0].token = "unknown";
    auto invalid = task.start(input);
    BOOST_REQUIRE(!invalid);
    BOOST_CHECK(invalid.error().code() == srt::Error::InvalidArgument);
    BOOST_CHECK(trace->calls.empty());
    input.words[0].phones[0].token = "a";
    input.words[0].phones.push_back({"a", "", 0, 0.02});
    auto shortResult = task.start(input);
    BOOST_REQUIRE(!shortResult);
    BOOST_CHECK(shortResult.error().code() == ds::ErrorCode::ShapeMismatch);
    BOOST_CHECK(task.state() == srt::ITask::Failed);
}

BOOST_AUTO_TEST_CASE(nullOutputTensorsFailWithoutDereferencingThem) {
    auto trace = std::make_shared<ds::test::DriverTrace>();
    trace->fault = ds::test::SessionFault::NullTensor;
    trace->faultModel = "predictor.onnx";
    ds::test::DeclaredInference declaration(std::make_unique<ds::test::RecordingDriver>(trace),
                                            executionConfiguration());
    ds::DurationInference executive(declaration.spec());
    ds::DurationTask task(executive);
    BOOST_REQUIRE(task.initialize(Contract::InitArgs()));
    auto failed = task.start(makeInput());
    BOOST_REQUIRE(!failed);
    BOOST_CHECK(failed.error().code() == ds::ErrorCode::SessionFailed);
    BOOST_CHECK(task.state() == srt::ITask::Failed);
    trace->fault = ds::test::SessionFault::None;
    auto recovered = task.start(makeInput());
    BOOST_REQUIRE(recovered);
    BOOST_CHECK(task.state() == srt::ITask::Succeeded);
}
