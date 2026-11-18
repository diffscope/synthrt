#include <atomic>
#include <cstring>
#include <vector>

#include <dsinfer/Support/ErrorCode.h>

#include <memory>
#include <string>

#include <pitch/PitchInterpreter.h>
#include <pitch/PitchInference.h>
#include <pitch/PitchTask.h>

namespace Contract = ds::Api::Pitch::L1;
using TestInterpreter = ds::PitchInterpreter;

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
    ds::PitchInference executive(declaration.spec(), {}, {});
    ds::PitchTask task(executive);
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
        input.parameters.push_back({ds::Api::Common::L1::Tags::Pitch, {69}, 0.01});
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
    ds::PitchInference executive(declaration.spec(), {}, {});
    ds::PitchTask task(executive);
    BOOST_REQUIRE(task.initialize(Contract::InitArgs()));
    auto input = makeInput();
    auto result = task.start(input);
    BOOST_REQUIRE_MESSAGE(result, (result ? std::string() : result.error().message()));
    BOOST_CHECK(task.state() == srt::ITask::Succeeded);
    BOOST_CHECK(result.value()->pitch == std::vector<double>({2, 2, 2, 2}));
    BOOST_CHECK_CLOSE(result.value()->interval, 0.01, 0.0001);
    BOOST_REQUIRE_EQUAL(trace->calls.size(), 2u);
    const auto &inputs = trace->calls.back().inputs;
    BOOST_CHECK_EQUAL(inputs.at("pitch")->data<float>()[0], 69);
    BOOST_CHECK_EQUAL(inputs.at("expr")->data<float>()[0], 1);
    BOOST_CHECK_EQUAL(inputs.at("note_dur")->data<int64_t>()[0], 4);
    BOOST_CHECK_EQUAL(inputs.at("speedup")->data<int64_t>()[0], 50);
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
        ds::PitchInference executive(declaration.spec(), {}, {});
        {
            ds::PitchTask task(executive);
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
        ds::PitchInference executive(declaration.spec(), {}, {});
        ds::PitchTask task(executive);
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
    ds::PitchInference executive(declaration.spec(), {}, {});
    ds::PitchTask task(executive);
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
    ds::PitchInference executive(declaration.spec(), {}, {});
    ds::PitchTask task(executive);
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
    ds::PitchInference executive(declaration.spec(), {}, {});
    auto task = std::make_unique<ds::PitchTask>(executive);
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
    ds::PitchInference executive(declaration.spec(), {}, {});
    ds::PitchTask task(executive);
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
    ds::PitchInference executive(declaration.spec(), {}, {});
    ds::PitchTask task(executive);
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
BOOST_AUTO_TEST_CASE(noteRestsCentsAndRetakeMasksReachThePredictor) {
    namespace Co = ds::Api::Common::L1;
    auto trace = std::make_shared<ds::test::DriverTrace>();
    ds::test::DeclaredInference declaration(std::make_unique<ds::test::RecordingDriver>(trace),
                                            executionConfiguration());
    ds::PitchInference executive(declaration.spec(), {}, {});
    ds::PitchTask task(executive);
    BOOST_REQUIRE(task.initialize(Contract::InitArgs()));
    auto input = makeInput();
    input.words[0].notes = {
        {0, 0, 0.01, Co::GlideType::None, true},
        {69, 50, 0.02},
        {0, 0, 0.01, Co::GlideType::None, true}
    };
    input.parameters[0].retake = Co::InputParameterInfo::RetakeRange{0.01, 0.03};
    input.parameters.push_back({Co::Tags::Expr, {0.25}, 0.01});
    BOOST_REQUIRE(task.start(input));
    const auto &inputs = trace->calls.back().inputs;
    BOOST_CHECK_EQUAL(inputs.at("expr")->data<float>()[0], 0.25f);
    BOOST_CHECK_EQUAL(inputs.at("note_midi")->data<float>()[0], 69.5f);
    BOOST_CHECK_EQUAL(inputs.at("note_midi")->data<float>()[2], 69.5f);
    BOOST_CHECK_EQUAL(inputs.at("note_dur")->data<int64_t>()[1], 2);
    BOOST_CHECK(inputs.at("note_rest")->data<bool>()[0]);
    BOOST_CHECK(!inputs.at("note_rest")->data<bool>()[1]);
    const auto retake = inputs.at("retake")->data<bool>();
    BOOST_CHECK(!retake[0]);
    BOOST_CHECK(retake[1]);
    BOOST_CHECK(retake[2]);
    BOOST_CHECK(!retake[3]);
}

BOOST_AUTO_TEST_CASE(absentPitchAndExpressionProduceDefaultPredictorInputs) {
    auto trace = std::make_shared<ds::test::DriverTrace>();
    ds::test::DeclaredInference declaration(std::make_unique<ds::test::RecordingDriver>(trace),
                                            executionConfiguration());
    ds::PitchInference executive(declaration.spec(), {}, {});
    ds::PitchTask task(executive);
    BOOST_REQUIRE(task.initialize(Contract::InitArgs()));
    auto input = makeInput();
    input.parameters.clear();
    BOOST_REQUIRE(task.start(input));
    const auto &inputs = trace->calls.back().inputs;
    for (size_t i = 0; i < 4; ++i) {
        BOOST_CHECK_EQUAL(inputs.at("pitch")->data<float>()[i], 0);
        BOOST_CHECK_EQUAL(inputs.at("expr")->data<float>()[i], 1);
        BOOST_CHECK(inputs.at("retake")->data<bool>()[i]);
    }
}
BOOST_AUTO_TEST_CASE(linguisticModesLanguageIdsAndSpeakerEmbeddingsReachTheirModels) {
    for (const auto mode : {"word", "phoneme"}) {
        auto trace = std::make_shared<ds::test::DriverTrace>();
        auto configuration = executionConfiguration();
        configuration["linguisticMode"] = mode;
        configuration["useContinuousAcceleration"] = true;
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
        BOOST_REQUIRE_EQUAL(trace->calls.size(), 2u);
        const auto &encoder = trace->calls[0].inputs;
        BOOST_CHECK_EQUAL(encoder.at("languages")->data<int64_t>()[0], 7);
        BOOST_CHECK_EQUAL(encoder.count("word_div"), std::string(mode) == "word" ? 1u : 0u);
        const auto &predictor = trace->calls[1].inputs;
        BOOST_CHECK_EQUAL(predictor.at("steps")->data<int64_t>()[0], 20);
        BOOST_CHECK(predictor.at("spk_embed")->shape() == std::vector<int64_t>({1, 4, 2}));
        BOOST_CHECK_EQUAL(predictor.at("spk_embed")->data<float>()[1], 3);
    }
}

BOOST_AUTO_TEST_CASE(nullOutputTensorsFailWithoutDereferencingThem) {
    auto trace = std::make_shared<ds::test::DriverTrace>();
    trace->fault = ds::test::SessionFault::NullTensor;
    trace->faultModel = "predictor.onnx";
    ds::test::DeclaredInference declaration(std::make_unique<ds::test::RecordingDriver>(trace),
                                            executionConfiguration());
    ds::PitchInference executive(declaration.spec(), {}, {});
    ds::PitchTask task(executive);
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
