#include <atomic>
#include <cstring>
#include <vector>

#include <memory>
#include <string>

#include <acoustic/AcousticInterpreter.h>
#include <acoustic/AcousticInference.h>
#include <acoustic/AcousticTask.h>

namespace Contract = ds::Api::Acoustic::L1;
using TestInterpreter = ds::AcousticInterpreter;

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

BOOST_AUTO_TEST_CASE(task_rejects_wrong_payloads_and_null_async_input) {
    ds::test::DeclaredInference declaration;
    ds::AcousticInference executive(declaration.spec());
    ds::AcousticTask task(executive);
    OtherInit args;
    OtherInput input;
    auto initialized = task.initialize(static_cast<const srt::TaskInitArgs &>(args));
    BOOST_REQUIRE(!initialized);
    BOOST_CHECK(initialized.error().code() == srt::Error::InvalidArgument);
    auto result = task.start(static_cast<const srt::TaskStartInput &>(input));
    BOOST_REQUIRE(!result);
    BOOST_CHECK(result.error().code() == srt::Error::InvalidArgument);
    bool called = false;
    auto async = task.startAsync(std::shared_ptr<const srt::TaskStartInput>(),
                                 [&called](auto) { called = true; });
    BOOST_REQUIRE(!async);
    BOOST_CHECK(async.error().code() == srt::Error::InvalidArgument);
    BOOST_CHECK(!called);
    BOOST_REQUIRE(task.waitForFinished());
}

namespace {
    Contract::AcousticStartInput makeInput() {
        Contract::AcousticStartInput input;
        input.duration = 0.04;
        ds::Api::Common::L1::InputWordInfo word;
        word.phones.push_back({"a", "", 0, 0});
        word.notes.push_back({69, 0, 0.04});
        input.words.push_back(word);
        input.steps = 20;
        input.depth = 0.75f;
        input.parameters.push_back({ds::Api::Common::L1::Tags::Pitch, {69}, 0.01});
        return input;
    }

    srt::JsonObject executionConfiguration() {
        return srt::JsonObject{
            {"sampleRate",  100 },
            {"hopSize",     1   },
            {"melChannels", 2   },
            {"maxDepth",    1000}
        };
    }
}

BOOST_AUTO_TEST_CASE(modelInputsResultsAndReuse) {
    auto trace = std::make_shared<ds::test::DriverTrace>();
    ds::test::DeclaredInference declaration(std::make_unique<ds::test::RecordingDriver>(trace),
                                            executionConfiguration());
    ds::AcousticInference executive(declaration.spec());
    ds::AcousticTask task(executive);
    BOOST_REQUIRE(task.initialize(Contract::AcousticInitArgs()));
    auto input = makeInput();
    auto result = task.start(input);
    BOOST_REQUIRE_MESSAGE(result, (result ? std::string() : result.error().message()));
    BOOST_CHECK(task.state() == srt::ITask::Succeeded);
    BOOST_REQUIRE(result.value()->mel);
    BOOST_CHECK(result.value()->mel->shape() == std::vector<int64_t>({1, 4, 2}));
    BOOST_REQUIRE(result.value()->f0);
    BOOST_CHECK_CLOSE(result.value()->f0->data<float>()[0], 440.0f, 0.0001);
    BOOST_REQUIRE_EQUAL(trace->calls.size(), 1u);
    const auto &inputs = trace->calls.back().inputs;
    BOOST_CHECK_EQUAL(inputs.at("tokens")->data<int64_t>()[0], 1);
    BOOST_CHECK_EQUAL(inputs.at("durations")->data<int64_t>()[0], 4);
    BOOST_CHECK_EQUAL(inputs.at("speedup")->data<int64_t>()[0], 50);
    BOOST_CHECK_EQUAL(inputs.at("depth")->data<int64_t>()[0], 750);
    auto repeated = task.start(input);
    BOOST_REQUIRE(repeated);
    BOOST_CHECK(task.state() == srt::ITask::Succeeded);
}

BOOST_AUTO_TEST_CASE(openFailuresAreReportedAndInitializationCanBeRetried) {
    for (const auto model : {"model.onnx"}) {
        auto trace = std::make_shared<ds::test::DriverTrace>();
        trace->fault = ds::test::SessionFault::Open;
        trace->faultModel = model;
        ds::test::DeclaredInference declaration(std::make_unique<ds::test::RecordingDriver>(trace),
                                                executionConfiguration());
        ds::AcousticInference executive(declaration.spec());
        {
            ds::AcousticTask task(executive);
            auto result = task.initialize(Contract::AcousticInitArgs());
            BOOST_REQUIRE(!result);
            BOOST_CHECK(result.error().code() == srt::Error::FileNotOpen);
            BOOST_CHECK(task.state() == srt::ITask::Failed);
            trace->fault = ds::test::SessionFault::None;
            BOOST_REQUIRE(task.initialize(Contract::AcousticInitArgs()));
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
        trace->faultModel = "model.onnx";
        ds::test::DeclaredInference declaration(std::make_unique<ds::test::RecordingDriver>(trace),
                                                executionConfiguration());
        ds::AcousticInference executive(declaration.spec());
        ds::AcousticTask task(executive);
        BOOST_REQUIRE(task.initialize(Contract::AcousticInitArgs()));
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
    ds::AcousticInference executive(declaration.spec());
    ds::AcousticTask task(executive);
    BOOST_REQUIRE(task.initialize(Contract::AcousticInitArgs()));
    auto input = std::make_shared<Contract::AcousticStartInput>(makeInput());
    std::atomic<int> callbacks{0};
    bool succeeded = false;
    BOOST_REQUIRE(task.startAsync(
        input, [&](srt::Expected<std::unique_ptr<Contract::AcousticResult>> result) {
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
    ds::AcousticInference executive(declaration.spec());
    ds::AcousticTask task(executive);
    BOOST_REQUIRE(task.initialize(Contract::AcousticInitArgs()));
    auto input = std::make_shared<Contract::AcousticStartInput>(makeInput());
    std::atomic<int> callbacks{0};
    BOOST_REQUIRE(task.startAsync(
        input, [&](srt::Expected<std::unique_ptr<Contract::AcousticResult>>) { ++callbacks; }));
    BOOST_REQUIRE(trace->waitUntilEntered());
    auto duplicate =
        task.startAsync(input, [](srt::Expected<std::unique_ptr<Contract::AcousticResult>>) {});
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
    ds::AcousticInference executive(declaration.spec());
    auto task = std::make_unique<ds::AcousticTask>(executive);
    BOOST_REQUIRE(task->initialize(Contract::AcousticInitArgs()));
    auto input = std::make_shared<Contract::AcousticStartInput>(makeInput());
    std::atomic<int> callbacks{0};
    BOOST_REQUIRE(task->startAsync(
        input, [&](srt::Expected<std::unique_ptr<Contract::AcousticResult>>) { ++callbacks; }));
    BOOST_REQUIRE(trace->waitUntilEntered());
    task.reset();
    BOOST_CHECK_EQUAL(callbacks.load(), 1);
    BOOST_CHECK_GT(trace->stopped.load(), 0);
    BOOST_CHECK_EQUAL(trace->destroyed.load(), trace->created.load());
}

BOOST_AUTO_TEST_CASE(incompatibleResultVersionsAreRejectedBeforeCasting) {
    auto trace = std::make_shared<ds::test::DriverTrace>();
    trace->fault = ds::test::SessionFault::WrongVersion;
    trace->faultModel = "model.onnx";
    ds::test::DeclaredInference declaration(std::make_unique<ds::test::RecordingDriver>(trace),
                                            executionConfiguration());
    ds::AcousticInference executive(declaration.spec());
    ds::AcousticTask task(executive);
    BOOST_REQUIRE(task.initialize(Contract::AcousticInitArgs()));
    auto input = makeInput();
    auto result = task.start(input);
    BOOST_REQUIRE(!result);
    BOOST_CHECK(result.error().code() == srt::Error::InvalidArgument);
    BOOST_CHECK(task.state() == srt::ITask::Failed);
}
