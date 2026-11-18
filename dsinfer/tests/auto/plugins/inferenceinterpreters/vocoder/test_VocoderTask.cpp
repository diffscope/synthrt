#include <atomic>
#include <cstring>
#include <vector>

#include <dsinfer/Support/ErrorCode.h>

#include <memory>
#include <string>

#include <vocoder/VocoderInterpreter.h>
#include <vocoder/VocoderInference.h>
#include <vocoder/VocoderTask.h>

namespace Contract = ds::Api::Vocoder::L1;
using TestInterpreter = ds::VocoderInterpreter;

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
    ds::VocoderInference executive(declaration.spec());
    ds::VocoderTask task(executive);
    OtherInit args;
    OtherInput input;
    auto initialized = task.initialize(static_cast<const srt::TaskInitArgs &>(args));
    BOOST_REQUIRE(!initialized);
    BOOST_CHECK(initialized.error().code() == srt::Error::InvalidArgument);
    auto invalid = task.start(static_cast<const srt::TaskStartInput &>(input));
    BOOST_REQUIRE(!invalid);
    BOOST_CHECK(invalid.error().code() == srt::Error::InvalidArgument);
    Contract::StartInput valid;
    auto result = task.start(static_cast<const srt::TaskStartInput &>(valid));
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
        auto mel = ds::Tensor::createFilled<float>({1, 4, 2}, 0.5f);
        auto f0 = ds::Tensor::createFilled<float>({1, 4}, 440.0f);
        BOOST_REQUIRE(mel);
        BOOST_REQUIRE(f0);
        input.mel = mel.take();
        input.f0 = f0.take();
        return input;
    }

    srt::JsonObject executionConfiguration() {
        return srt::JsonObject();
    }
}

BOOST_AUTO_TEST_CASE(modelInputsResultsAndReuse) {
    auto trace = std::make_shared<ds::test::DriverTrace>();
    ds::test::DeclaredInference declaration(std::make_unique<ds::test::RecordingDriver>(trace),
                                            executionConfiguration());
    ds::VocoderInference executive(declaration.spec());
    ds::VocoderTask task(executive);
    BOOST_REQUIRE(task.initialize(Contract::InitArgs()));
    auto input = makeInput();
    auto result = task.start(input);
    BOOST_REQUIRE_MESSAGE(result, (result ? std::string() : result.error().message()));
    BOOST_CHECK(task.state() == srt::ITask::Succeeded);
    const std::vector<float> waveform{2, 2, 2, 2};
    BOOST_REQUIRE_EQUAL(result.value()->audioData.size(), waveform.size() * sizeof(float));
    BOOST_CHECK_EQUAL(std::memcmp(result.value()->audioData.data(), waveform.data(),
                                  result.value()->audioData.size()),
                      0);
    BOOST_REQUIRE_EQUAL(trace->calls.size(), 1u);
    BOOST_CHECK(trace->calls[0].inputs.at("mel") == input.mel);
    BOOST_CHECK(trace->calls[0].inputs.at("f0") == input.f0);
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
        ds::VocoderInference executive(declaration.spec());
        {
            ds::VocoderTask task(executive);
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
        trace->faultModel = "model.onnx";
        ds::test::DeclaredInference declaration(std::make_unique<ds::test::RecordingDriver>(trace),
                                                executionConfiguration());
        ds::VocoderInference executive(declaration.spec());
        ds::VocoderTask task(executive);
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
    ds::VocoderInference executive(declaration.spec());
    ds::VocoderTask task(executive);
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
    ds::VocoderInference executive(declaration.spec());
    ds::VocoderTask task(executive);
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
    ds::VocoderInference executive(declaration.spec());
    auto task = std::make_unique<ds::VocoderTask>(executive);
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
    trace->faultModel = "model.onnx";
    ds::test::DeclaredInference declaration(std::make_unique<ds::test::RecordingDriver>(trace),
                                            executionConfiguration());
    ds::VocoderInference executive(declaration.spec());
    ds::VocoderTask task(executive);
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
    ds::VocoderInference executive(declaration.spec());
    ds::VocoderTask task(executive);
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

BOOST_AUTO_TEST_CASE(nullOutputTensorsFailWithoutDereferencingThem) {
    auto trace = std::make_shared<ds::test::DriverTrace>();
    trace->fault = ds::test::SessionFault::NullTensor;
    trace->faultModel = "model.onnx";
    ds::test::DeclaredInference declaration(std::make_unique<ds::test::RecordingDriver>(trace),
                                            executionConfiguration());
    ds::VocoderInference executive(declaration.spec());
    ds::VocoderTask task(executive);
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
