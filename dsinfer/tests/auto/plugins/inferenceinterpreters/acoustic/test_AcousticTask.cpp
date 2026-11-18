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
    ds::AcousticInference executive(declaration.spec(), {}, {});
    ds::AcousticTask task(executive);
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
    ds::AcousticInference executive(declaration.spec(), {}, {});
    ds::AcousticTask task(executive);
    BOOST_REQUIRE(task.initialize(Contract::InitArgs()));
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
        ds::AcousticInference executive(declaration.spec(), {}, {});
        {
            ds::AcousticTask task(executive);
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
        ds::AcousticInference executive(declaration.spec(), {}, {});
        ds::AcousticTask task(executive);
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
    ds::AcousticInference executive(declaration.spec(), {}, {});
    ds::AcousticTask task(executive);
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
    ds::AcousticInference executive(declaration.spec(), {}, {});
    ds::AcousticTask task(executive);
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
    ds::AcousticInference executive(declaration.spec(), {}, {});
    auto task = std::make_unique<ds::AcousticTask>(executive);
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
    ds::AcousticInference executive(declaration.spec(), {}, {});
    ds::AcousticTask task(executive);
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
    ds::AcousticInference executive(declaration.spec(), {}, {});
    ds::AcousticTask task(executive);
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
BOOST_AUTO_TEST_CASE(f0PrecedenceToneShiftAndContinuousAcceleration) {
    auto trace = std::make_shared<ds::test::DriverTrace>();
    auto configuration = executionConfiguration();
    configuration["useContinuousAcceleration"] = true;
    configuration["useVariableDepth"] = true;
    ds::test::DeclaredInference declaration(std::make_unique<ds::test::RecordingDriver>(trace),
                                            configuration);
    ds::AcousticInference executive(declaration.spec(), {}, {});
    ds::AcousticTask task(executive);
    BOOST_REQUIRE(task.initialize(Contract::InitArgs()));
    auto input = makeInput();
    input.parameters.push_back({ds::Api::Common::L1::Tags::F0, {220}, 0.01});
    input.parameters.push_back({ds::Api::Common::L1::Tags::ToneShift, {1200}, 0.01});
    auto result = task.start(input);
    BOOST_REQUIRE(result);
    BOOST_CHECK_CLOSE(result.value()->f0->data<float>()[0], 440.0f, 0.0001);
    const auto &inputs = trace->calls.back().inputs;
    BOOST_CHECK_EQUAL(inputs.at("steps")->data<int64_t>()[0], 20);
    BOOST_CHECK_EQUAL(inputs.at("depth")->data<float>()[0], 0.75f);
    BOOST_CHECK_EQUAL(inputs.count("speedup"), 0u);
    for (const auto steps : {0, -1}) {
        input.steps = steps;
        auto rejected = task.start(input);
        BOOST_REQUIRE(!rejected);
        BOOST_CHECK(rejected.error().code() == srt::Error::InvalidArgument);
    }
    BOOST_CHECK_EQUAL(trace->calls.size(), 1u);
}

BOOST_AUTO_TEST_CASE(languageAndSpeakerInputsAreResolvedAndValidated) {
    auto trace = std::make_shared<ds::test::DriverTrace>();
    auto configuration = executionConfiguration();
    configuration["useLanguageId"] = true;
    configuration["languages"] = "languages.json";
    configuration["useSpeakerEmbedding"] = true;
    configuration["hiddenSize"] = 2;
    configuration["speakers"] = srt::JsonObject{
        {"lead", "speaker.emb"}
    };
    ds::test::DeclaredInference declaration(std::make_unique<ds::test::RecordingDriver>(trace),
                                            configuration);
    Contract::ImportOptions importOptions;
    importOptions.speakerMapping.emplace("host", "lead");
    auto made = declaration.spec().createInference(importOptions, Contract::RuntimeOptions());
    BOOST_REQUIRE(made);
    auto executive = made.take();
    auto task = executive->as<Contract::Executive>();
    BOOST_REQUIRE(task->initialize(Contract::InitArgs()));
    auto input = makeInput();
    input.words[0].phones[0].language = "eng";
    input.speakers.push_back({"host", 0.01, {1}});
    BOOST_REQUIRE(task->start(input));
    const auto &inputs = trace->calls.back().inputs;
    BOOST_CHECK_EQUAL(inputs.at("languages")->data<int64_t>()[0], 7);
    BOOST_CHECK(inputs.at("spk_embed")->shape() == std::vector<int64_t>({1, 4, 2}));
    BOOST_CHECK_EQUAL(inputs.at("spk_embed")->data<float>()[0], 1);
    BOOST_CHECK_EQUAL(inputs.at("spk_embed")->data<float>()[1], 3);
    input.words[0].phones[0].language = "unknown";
    auto language = task->start(input);
    BOOST_REQUIRE(!language);
    BOOST_CHECK(language.error().code() == srt::Error::InvalidArgument);
    input.words[0].phones[0].language = "eng";
    input.speakers[0].name = "unknown";
    auto speaker = task->start(input);
    BOOST_REQUIRE(!speaker);
    BOOST_CHECK(speaker.error().code() == srt::Error::InvalidArgument);
    input.speakers.clear();
    auto absent = task->start(input);
    BOOST_REQUIRE(!absent);
    BOOST_CHECK(absent.error().code() == ds::ErrorCode::InvalidInput);
    BOOST_CHECK_EQUAL(trace->calls.size(), 1u);
}

BOOST_AUTO_TEST_CASE(requiredControlsAndEmptyOptionalControlsFollowTheirContracts) {
    namespace Tags = ds::Api::Common::L1::Tags;
    auto trace = std::make_shared<ds::test::DriverTrace>();
    auto configuration = executionConfiguration();
    configuration["parameters"] = srt::JsonArray{
        "gender", "velocity", "energy", "breathiness", "voicing", "tension", "mouth_opening"};
    ds::test::DeclaredInference declaration(std::make_unique<ds::test::RecordingDriver>(trace),
                                            configuration);
    ds::AcousticInference executive(declaration.spec(), {}, {});
    ds::AcousticTask task(executive);
    BOOST_REQUIRE(task.initialize(Contract::InitArgs()));
    auto input = makeInput();
    input.parameters.push_back({Tags::Gender, {}, 0.01});
    input.parameters.push_back({Tags::Velocity, {}, 0.01});
    for (const auto &tag :
         {Tags::Energy, Tags::Breathiness, Tags::Voicing, Tags::Tension, Tags::MouthOpening}) {
        input.parameters.push_back({tag, {0.5}, 0.01});
    }
    BOOST_REQUIRE(task.start(input));
    BOOST_CHECK_EQUAL(trace->calls.back().inputs.at("gender")->data<float>()[0], 0);
    BOOST_CHECK_EQUAL(trace->calls.back().inputs.at("velocity")->data<float>()[0], 1);
    BOOST_CHECK_EQUAL(trace->calls.back().inputs.at("mouth_opening")->data<float>()[0], 0.5f);
    const auto complete = input.parameters;
    for (size_t index = 3; index < 7; ++index) {
        input.parameters = complete;
        input.parameters.erase(input.parameters.begin() + index);
        auto missing = task.start(input);
        BOOST_REQUIRE(!missing);
        BOOST_CHECK(missing.error().code() == ds::ErrorCode::InvalidInput);
    }
    BOOST_CHECK_EQUAL(trace->calls.size(), 1u);
}

BOOST_AUTO_TEST_CASE(nullOutputTensorsFailWithoutDereferencingThem) {
    auto trace = std::make_shared<ds::test::DriverTrace>();
    trace->fault = ds::test::SessionFault::NullTensor;
    trace->faultModel = "model.onnx";
    ds::test::DeclaredInference declaration(std::make_unique<ds::test::RecordingDriver>(trace),
                                            executionConfiguration());
    ds::AcousticInference executive(declaration.spec(), {}, {});
    ds::AcousticTask task(executive);
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
