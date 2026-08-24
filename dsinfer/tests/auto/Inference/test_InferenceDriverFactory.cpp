#include <array>
#include <cstddef>
#include <filesystem>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>

#include <stdcorelib/plugin/plugin.h>
#include <stdcorelib/support/json.h>

#include <synthrt/Core/SynthUnit.h>

#include <dsinfer/Api/Drivers/Onnx/OnnxDriverApi.h>
#include <dsinfer/Core/Tensor.h>
#include <dsinfer/Inference/InferenceDriverFactory.h>
#include <dsinfer/Inference/InferenceDriverPlugin.h>
#include <dsinfer/Inference/InferenceSession.h>
#include <dsinfer/Support/ErrorCode.h>

#include "OnnxTensor.h"

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

namespace {

    class TestInitArgs : public ds::InferenceDriverInitArgs {
    public:
        TestInitArgs() : InferenceDriverInitArgs("test", 1) {
        }
    };

    class TestDriver : public ds::InferenceDriver {
    public:
        explicit TestDriver(const std::string &backend) : InferenceDriver(backend) {
        }

        srt::Expected<void> initialize(const ds::InferenceDriverInitArgs &args) override {
            if (args.type() != "test" || args.version() != 1) {
                return srt::Error(srt::Error::InvalidArgument, "unexpected test arguments");
            }
            m_initialized = true;
            return {};
        }

        std::unique_ptr<ds::InferenceSession> createSession() override {
            return {};
        }

        bool isInitialized() const noexcept {
            return m_initialized;
        }

    private:
        bool m_initialized = false;
    };

    class TestDriverPlugin : public ds::InferenceDriverPlugin {
    public:
        explicit TestDriverPlugin(std::string backend) : m_backend(std::move(backend)) {
        }

        srt::Expected<std::unique_ptr<ds::InferenceDriver>> create() override {
            return std::unique_ptr<ds::InferenceDriver>(new TestDriver(m_backend));
        }

    private:
        std::string m_backend;
    };

    stdc::json::Value manifest(std::string backend) {
        return stdc::json::Object{
            {"iid",      ds::InferenceDriverPlugin::IID                     },
            {"name",     "test-driver"                                      },
            {"metadata", stdc::json::Object{{"backend", std::move(backend)}}},
        };
    }

#ifdef DSINFER_TEST_ONNX_MODEL_PATH
    template <typename T, size_t Size>
    std::shared_ptr<ds::Tensor> makeTensor(std::array<T, Size> values) {
        auto result = ds::Tensor::createFromView<T>(
            {1, static_cast<int64_t>(Size)}, stdc::array_view<T>(values.data(), values.size()));
        if (!result) {
            throw std::runtime_error(result.error().toString());
        }
        return result.take();
    }

    std::shared_ptr<ds::Api::Onnx::SessionStartInput> makeSessionInput() {
        auto input = std::make_shared<ds::Api::Onnx::SessionStartInput>();
        input->inputs.emplace("input_f1", makeTensor(std::array{3.14f, -2.7f, 0.0f, 11.4f}));
        input->inputs.emplace("input_f2", makeTensor(std::array{1.85f, 2.7f, -5.5f, 5.14f}));
        input->inputs.emplace("input_i1", makeTensor(std::array<int64_t, 4>{7, -3, 0, 100}));
        input->inputs.emplace("input_i2", makeTensor(std::array<int64_t, 4>{-2, 9, 5, -50}));
        input->inputs.emplace("input_b1",
                              makeTensor(std::array<bool, 4>{true, true, false, false}));
        input->inputs.emplace("input_b2",
                              makeTensor(std::array<bool, 4>{false, true, true, false}));
        input->outputs = {"output_f", "output_i", "output_b"};
        return input;
    }

    void checkSessionResult(const ds::Api::Onnx::SessionResult &result) {
        BOOST_REQUIRE_EQUAL(result.outputs.size(), 3u);

        const auto &floats = result.outputs.at("output_f");
        BOOST_REQUIRE(floats);
        BOOST_CHECK(floats->dataType() == ds::ITensor::Float);
        BOOST_REQUIRE_EQUAL(floats->elementCount(), 4u);
        const auto *floatData = floats->constData<float>();
        BOOST_REQUIRE(floatData);
        BOOST_CHECK_CLOSE(floatData[0], 4.99f, 0.001);
        BOOST_CHECK_SMALL(floatData[1], 0.0001f);
        BOOST_CHECK_CLOSE(floatData[2], -5.5f, 0.001);
        BOOST_CHECK_CLOSE(floatData[3], 16.54f, 0.001);

        const auto &integers = result.outputs.at("output_i");
        BOOST_REQUIRE(integers);
        BOOST_CHECK(integers->dataType() == ds::ITensor::Int64);
        const auto *integerData = integers->constData<int64_t>();
        BOOST_REQUIRE(integerData);
        const std::array<int64_t, 4> expectedIntegers{5, 6, 5, 50};
        BOOST_CHECK_EQUAL_COLLECTIONS(integerData, integerData + 4, expectedIntegers.begin(),
                                      expectedIntegers.end());

        const auto &booleans = result.outputs.at("output_b");
        BOOST_REQUIRE(booleans);
        BOOST_CHECK(booleans->dataType() == ds::ITensor::Bool);
        const auto *booleanData = booleans->constData<bool>();
        BOOST_REQUIRE(booleanData);
        BOOST_CHECK(booleanData[0]);
        BOOST_CHECK(!booleanData[1]);
        BOOST_CHECK(booleanData[2]);
        BOOST_CHECK(!booleanData[3]);
    }
#endif

}

BOOST_AUTO_TEST_SUITE(test_InferenceDriverFactory)

BOOST_AUTO_TEST_CASE(test_DiscoversAndCreatesRuntimeService) {
    TestDriverPlugin plugin("test");
    ds::InferenceDriverFactory factory;
    factory.addRuntimePlugin(&plugin, manifest("test"));

    const auto backends = factory.backends();
    BOOST_REQUIRE_EQUAL(backends.size(), 1u);
    BOOST_CHECK_EQUAL(backends[0], "test");

    auto result = factory.create("test");
    BOOST_REQUIRE(result);
    auto driver = result.take();
    BOOST_CHECK_EQUAL(driver->iid(), ds::InferenceDriver::IID);
    BOOST_CHECK_EQUAL(driver->name(), "test");
    BOOST_CHECK_EQUAL(driver->backend(), "test");
    TestInitArgs args;
    BOOST_REQUIRE(driver->initialize(args));
    BOOST_CHECK(driver->as<TestDriver>()->isInitialized());

    auto *driverPointer = driver.get();
    srt::SynthUnit unit;
    BOOST_REQUIRE(unit.addRuntimeService(std::move(driver)));
    BOOST_CHECK(unit.runtimeService(ds::InferenceDriver::IID, "test") == driverPointer);
    BOOST_CHECK(&driverPointer->synthUnit() == &unit);
}

BOOST_AUTO_TEST_CASE(test_RejectsMissingAndMismatchedDrivers) {
    TestDriverPlugin wrongPlugin("actual");
    ds::InferenceDriverFactory factory;

    auto missing = factory.create("missing");
    BOOST_REQUIRE(!missing);
    BOOST_CHECK(missing.error().code() == srt::Error::FileNotFound);

    factory.addRuntimePlugin(&wrongPlugin, manifest("claimed"));
    auto mismatched = factory.create("claimed");
    BOOST_REQUIRE(!mismatched);
    BOOST_CHECK(mismatched.error().code() == srt::Error::InvalidFormat);

    auto invalid = factory.create("invalid/name");
    BOOST_REQUIRE(!invalid);
    BOOST_CHECK(invalid.error().code() == srt::Error::InvalidArgument);
}

#ifdef DSINFER_TEST_DRIVER_PLUGIN_PATH
BOOST_AUTO_TEST_CASE(test_LoadsOnnxDriverBundle) {
    ds::InferenceDriverFactory factory;
    const std::array<std::filesystem::path, 1> pluginPaths{DSINFER_TEST_DRIVER_PLUGIN_PATH};
    factory.setPluginPaths(pluginPaths);

    const auto backends = factory.backends();
    BOOST_REQUIRE_EQUAL(backends.size(), 1u);
    BOOST_CHECK_EQUAL(backends.front(), ds::Api::Onnx::API_NAME);

    auto result = factory.create(ds::Api::Onnx::API_NAME);
    BOOST_REQUIRE(result);
    auto driver = result.take();
    BOOST_CHECK_EQUAL(driver->backend(), "onnx");
    BOOST_CHECK(driver->extension() == nullptr);
    BOOST_CHECK(driver->createSession() == nullptr);

    auto invalidResult = factory.create(ds::Api::Onnx::API_NAME);
    BOOST_REQUIRE(invalidResult);
    auto invalidDriver = invalidResult.take();
    ds::Api::Onnx::DriverInitArgs invalidArgs;
    invalidArgs.runtimeApi = ds::Api::Onnx::RuntimeApi{};
    auto invalidInitialization = invalidDriver->initialize(invalidArgs);
    BOOST_REQUIRE(!invalidInitialization);
    BOOST_CHECK(invalidInitialization.error().code() == srt::Error::InvalidArgument);

    ds::Api::Onnx::DriverInitArgs initArgs;
    initArgs.runtimePath =
        pluginPaths.front() / ds::Api::Onnx::API_NAME / "runtimes" / "onnx" / "default";
    BOOST_REQUIRE(driver->initialize(initArgs));
    auto duplicateInitialization = driver->initialize(initArgs);
    BOOST_REQUIRE(!duplicateInitialization);
    BOOST_CHECK(duplicateInitialization.error().code() == srt::Error::FileDuplicated);

    auto *driverPointer = driver.get();
    srt::SynthUnit unit;
    BOOST_REQUIRE(unit.addRuntimeService(std::move(driver)));
    BOOST_CHECK(unit.runtimeService(ds::InferenceDriver::IID, ds::Api::Onnx::API_NAME) ==
                driverPointer);

    auto *extension = driverPointer->extension()->as<ds::Api::Onnx::DriverExtension>();
    Ort::InitApi(extension->runtimeApi.ortApi);
    auto scalarResult = ds::OnnxTensor::createScalar<float>(2.5f, true);
    BOOST_REQUIRE(scalarResult);
    const auto scalar = scalarResult.take();
    BOOST_CHECK(scalar->shape().empty());
    BOOST_REQUIRE(scalar->constData<float>());
    BOOST_CHECK_EQUAL(*scalar->constData<float>(), 2.5f);
    const std::array<float, 2> invalidStorage{1.0f, 2.0f};
    const stdc::array_view<std::byte> invalidStorageView{
        reinterpret_cast<const std::byte *>(invalidStorage.data()), sizeof(invalidStorage)};
    auto mismatchedTensor =
        ds::OnnxTensor::createFromRawView(ds::ITensor::Float, {1}, invalidStorageView);
    BOOST_REQUIRE(!mismatchedTensor);
    BOOST_CHECK(mismatchedTensor.error().code() == srt::Error::InvalidArgument);

    auto externalResult = factory.create(ds::Api::Onnx::API_NAME);
    BOOST_REQUIRE(externalResult);
    auto externalDriver = externalResult.take();
    ds::Api::Onnx::DriverInitArgs externalArgs;
    externalArgs.runtimeApi = extension->runtimeApi;
    BOOST_REQUIRE(externalDriver->initialize(externalArgs));
    auto *externalExtension = externalDriver->extension()->as<ds::Api::Onnx::DriverExtension>();
    BOOST_CHECK(externalExtension->runtimeApi.ortApiBase == extension->runtimeApi.ortApiBase);
    BOOST_CHECK(externalExtension->runtimeApi.ortApi == extension->runtimeApi.ortApi);
    BOOST_CHECK_EQUAL(externalExtension->runtimeApi.ortApiVersion,
                      extension->runtimeApi.ortApiVersion);

    auto firstSession = driverPointer->createSession();
    auto secondSession = driverPointer->createSession();
    auto externalSession = externalDriver->createSession();
    BOOST_REQUIRE(firstSession);
    BOOST_REQUIRE(secondSession);
    BOOST_REQUIRE(externalSession);
    BOOST_CHECK_EQUAL(firstSession->id(), 1);
    BOOST_CHECK_EQUAL(secondSession->id(), 2);
    BOOST_CHECK_EQUAL(externalSession->id(), 1);
    BOOST_CHECK(!firstSession->isOpen());
    BOOST_CHECK(!externalSession->isOpen());

    ds::Api::Onnx::SessionOpenArgs openArgs;
    auto invalidModelSession = driverPointer->createSession();
    BOOST_REQUIRE(invalidModelSession);
    auto invalidModel = invalidModelSession->open(
        pluginPaths.front() / ds::Api::Onnx::API_NAME / "plugin.json", openArgs);
    BOOST_REQUIRE(!invalidModel);
    BOOST_CHECK(invalidModel.error().code() == srt::Error::InvalidFormat);

    externalDriver.reset();
    BOOST_CHECK_EQUAL(externalSession->id(), 1);
    BOOST_CHECK(!externalSession->isOpen());

    auto coreMlResult = factory.create(ds::Api::Onnx::API_NAME);
    BOOST_REQUIRE(coreMlResult);
    auto coreMlDriver = coreMlResult.take();
    ds::Api::Onnx::DriverInitArgs coreMlArgs;
    coreMlArgs.runtimeApi = extension->runtimeApi;
    coreMlArgs.ep = ds::Api::Onnx::ExecutionProvider::CoreML;
    auto coreMlInitialization = coreMlDriver->initialize(coreMlArgs);
    BOOST_REQUIRE(!coreMlInitialization);
    BOOST_CHECK(coreMlInitialization.error().code() == srt::Error::FeatureNotSupported);
}

#  ifdef DSINFER_TEST_ONNX_MODEL_PATH
BOOST_AUTO_TEST_CASE(test_RunsOnnxSessionsSynchronouslyAndAsynchronously) {
    ds::InferenceDriverFactory factory;
    const std::array<std::filesystem::path, 1> pluginPaths{DSINFER_TEST_DRIVER_PLUGIN_PATH};
    factory.setPluginPaths(pluginPaths);

    auto driverResult = factory.create(ds::Api::Onnx::API_NAME);
    BOOST_REQUIRE(driverResult);
    auto driver = driverResult.take();
    ds::Api::Onnx::DriverInitArgs initArgs;
    initArgs.runtimePath =
        pluginPaths.front() / ds::Api::Onnx::API_NAME / "runtimes" / "onnx" / "default";
    BOOST_REQUIRE(driver->initialize(initArgs));

    ds::Api::Onnx::SessionOpenArgs openArgs;
    auto firstConcurrentSession = driver->createSession();
    auto secondConcurrentSession = driver->createSession();
    BOOST_REQUIRE(firstConcurrentSession);
    BOOST_REQUIRE(secondConcurrentSession);
    auto firstOpen = std::async(std::launch::async, [&] {
        return firstConcurrentSession->open(DSINFER_TEST_ONNX_MODEL_PATH, openArgs);
    });
    auto secondOpen = std::async(std::launch::async, [&] {
        return secondConcurrentSession->open(DSINFER_TEST_ONNX_MODEL_PATH, openArgs);
    });
    BOOST_REQUIRE(firstOpen.get());
    BOOST_REQUIRE(secondOpen.get());
    BOOST_REQUIRE(firstConcurrentSession->close());
    BOOST_REQUIRE(secondConcurrentSession->close());

    auto session = driver->createSession();
    BOOST_REQUIRE(session);
    BOOST_REQUIRE(session->open(DSINFER_TEST_ONNX_MODEL_PATH, openArgs));

    auto missingInput = makeSessionInput();
    missingInput->inputs.erase("input_f1");
    auto missingInputResult = session->start(*missingInput);
    BOOST_REQUIRE(!missingInputResult);
    BOOST_CHECK(missingInputResult.error().code() == ds::ErrorCode::InvalidInput);

    auto unknownOutput = makeSessionInput();
    unknownOutput->outputs.insert("unknown_output");
    auto unknownOutputResult = session->start(*unknownOutput);
    BOOST_REQUIRE(!unknownOutputResult);
    BOOST_CHECK(unknownOutputResult.error().code() == ds::ErrorCode::InvalidInput);

    auto nullInput = makeSessionInput();
    nullInput->inputs["input_f1"].reset();
    auto nullInputResult = session->start(*nullInput);
    BOOST_REQUIRE(!nullInputResult);
    BOOST_CHECK(nullInputResult.error().code() == ds::ErrorCode::InvalidInput);

    auto missingCallback = session->startAsync(makeSessionInput(), {});
    BOOST_REQUIRE(!missingCallback);
    BOOST_CHECK(missingCallback.error().code() == srt::Error::InvalidArgument);

    auto input = makeSessionInput();
    auto syncResult = session->start(*input);
    BOOST_REQUIRE(syncResult);
    checkSessionResult(*syncResult.take()->as<ds::Api::Onnx::SessionResult>());
    BOOST_CHECK(session->state() == srt::ITask::Succeeded);

    std::promise<srt::Expected<std::unique_ptr<srt::TaskResult>>> promise;
    auto future = promise.get_future();
    BOOST_REQUIRE(session->startAsync(makeSessionInput(), [&promise](auto result) mutable {
        promise.set_value(std::move(result));
    }));
    auto asyncResult = future.get();
    BOOST_REQUIRE(session->waitForFinished());
    BOOST_REQUIRE(asyncResult);
    checkSessionResult(*asyncResult.take()->as<ds::Api::Onnx::SessionResult>());
    BOOST_CHECK(session->state() == srt::ITask::Succeeded);

    std::promise<void> callbackEntered;
    auto callbackEnteredFuture = callbackEntered.get_future();
    std::promise<void> releaseCallback;
    auto releaseCallbackFuture = releaseCallback.get_future().share();
    std::promise<srt::Expected<std::unique_ptr<srt::TaskResult>>> gatedPromise;
    auto gatedFuture = gatedPromise.get_future();
    BOOST_REQUIRE(session->startAsync(makeSessionInput(), [&callbackEntered, releaseCallbackFuture,
                                                           &gatedPromise](auto result) mutable {
        callbackEntered.set_value();
        releaseCallbackFuture.wait();
        gatedPromise.set_value(std::move(result));
    }));
    callbackEnteredFuture.get();
    auto overlappingResult = session->start(*makeSessionInput());
    BOOST_REQUIRE(!overlappingResult);
    BOOST_CHECK(overlappingResult.error().code() == ds::ErrorCode::SessionFailed);
    BOOST_CHECK(session->state() == srt::ITask::Succeeded);
    releaseCallback.set_value();
    BOOST_REQUIRE(gatedFuture.get());
    BOOST_REQUIRE(session->waitForFinished());

    auto selfDestroyingSession = driver->createSession();
    BOOST_REQUIRE(selfDestroyingSession);
    BOOST_REQUIRE(selfDestroyingSession->open(DSINFER_TEST_ONNX_MODEL_PATH, openArgs));
    std::promise<srt::Expected<std::unique_ptr<srt::TaskResult>>> destroyedPromise;
    auto destroyedFuture = destroyedPromise.get_future();
    BOOST_REQUIRE(selfDestroyingSession->startAsync(
        makeSessionInput(), [&selfDestroyingSession, &destroyedPromise](auto result) mutable {
            selfDestroyingSession.reset();
            destroyedPromise.set_value(std::move(result));
        }));
    auto destroyedResult = destroyedFuture.get();
    BOOST_REQUIRE(destroyedResult);
    BOOST_CHECK(!selfDestroyingSession);

    BOOST_REQUIRE(session->close());
    BOOST_CHECK(!session->isOpen());
}
#  endif
#endif

BOOST_AUTO_TEST_SUITE_END()
