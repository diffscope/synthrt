#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <future>
#include <memory>
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
#include "TestCaseLoader.h"

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

    stdc::json::Value metadata(std::string backend) {
        return stdc::json::Object{
            {"name",    "test-driver"     },
            {"backend", std::move(backend)},
        };
    }

#ifdef TEST_RESOURCE_DIRECTORY
    const auto testResourceDirectory = std::filesystem::path(TEST_RESOURCE_DIRECTORY);
    const auto testCasePath = testResourceDirectory / "mixed_type_ops.json";

    test::TestCaseData loadTestCase() {
        return test::TestCaseLoader::load(testCasePath);
    }

    std::shared_ptr<ds::Api::Onnx::SessionStartInput> makeSessionInput() {
        return loadTestCase().input;
    }

    void checkTensor(const ds::ITensor &actual, const ds::ITensor &expected) {
        BOOST_REQUIRE(actual.dataType() == expected.dataType());
        const auto actualShape = actual.shape();
        const auto expectedShape = expected.shape();
        BOOST_CHECK_EQUAL_COLLECTIONS(actualShape.begin(), actualShape.end(), expectedShape.begin(),
                                      expectedShape.end());
        BOOST_REQUIRE_EQUAL(actual.byteSize(), expected.byteSize());

        if (actual.dataType() == ds::ITensor::Float) {
            const auto actualData = actual.constData<float>();
            const auto expectedData = expected.constData<float>();
            BOOST_REQUIRE(actualData);
            BOOST_REQUIRE(expectedData);
            for (size_t i = 0; i < actual.elementCount(); ++i) {
                const auto difference = std::fabs(actualData[i] - expectedData[i]);
                const auto scale =
                    std::max({1.0f, std::fabs(actualData[i]), std::fabs(expectedData[i])});
                BOOST_CHECK_LE(difference, 1e-6f * scale);
            }
            return;
        }

        BOOST_CHECK_EQUAL(std::memcmp(actual.rawData(), expected.rawData(), actual.byteSize()), 0);
    }

    void checkSessionResult(const ds::Api::Onnx::SessionResult &actual,
                            const ds::Api::Onnx::SessionResult &expected) {
        BOOST_REQUIRE_EQUAL(actual.outputs.size(), expected.outputs.size());
        for (const auto &[name, expectedTensor] : expected.outputs) {
            const auto it = actual.outputs.find(name);
            BOOST_REQUIRE_MESSAGE(it != actual.outputs.end(), "missing output " << name);
            BOOST_REQUIRE(it->second);
            BOOST_REQUIRE(expectedTensor);
            checkTensor(*it->second, *expectedTensor);
        }
    }
#endif

}

BOOST_AUTO_TEST_SUITE(test_InferenceDriverFactory)

BOOST_AUTO_TEST_CASE(test_DiscoversAndCreatesRuntimeService) {
    TestDriverPlugin plugin("test");
    ds::InferenceDriverFactory factory;
    factory.addRuntimePlugin(ds::InferenceDriverPlugin::IID, &plugin, metadata("test"));

    const auto backends = factory.backends();
    BOOST_REQUIRE_EQUAL(backends.size(), 1u);
    BOOST_CHECK_EQUAL(backends[0], "test");
    auto loader = factory.find("test");
    BOOST_REQUIRE(loader);
    BOOST_CHECK(loader->filePath().empty());
    BOOST_CHECK(factory.find("missing") == nullptr);
    BOOST_CHECK(factory.find("invalid/name") == nullptr);

    auto result = factory.create(loader);
    BOOST_REQUIRE(result);
    auto driver = result.take();
    BOOST_CHECK_EQUAL(driver->iid(), ds::InferenceDriverPlugin::IID);
    BOOST_CHECK_EQUAL(driver->name(), "test");
    BOOST_CHECK_EQUAL(driver->backend(), "test");
    TestInitArgs args;
    BOOST_REQUIRE(driver->initialize(args));
    BOOST_CHECK(driver->as<TestDriver>()->isInitialized());

    auto driverPointer = driver.get();
    srt::SynthUnit unit;
    BOOST_REQUIRE(unit.addRuntimeService(std::move(driver)));
    BOOST_CHECK(unit.runtimeService(ds::InferenceDriverPlugin::IID, "test") == driverPointer);
    BOOST_CHECK(&driverPointer->synthUnit() == &unit);
}

BOOST_AUTO_TEST_CASE(test_RejectsMissingAndMismatchedDrivers) {
    TestDriverPlugin wrongPlugin("actual");
    ds::InferenceDriverFactory factory;

    BOOST_CHECK(factory.find("missing") == nullptr);
    BOOST_CHECK(factory.find("invalid/name") == nullptr);

    auto missing = factory.create(nullptr);
    BOOST_REQUIRE(!missing);
    BOOST_CHECK(missing.error().code() == srt::Error::InvalidArgument);

    factory.addRuntimePlugin(ds::InferenceDriverPlugin::IID, &wrongPlugin, metadata("claimed"));
    auto loader = factory.find("claimed");
    BOOST_REQUIRE(loader);
    ds::InferenceDriverFactory otherFactory;
    auto foreign = otherFactory.create(loader);
    BOOST_REQUIRE(!foreign);
    BOOST_CHECK(foreign.error().code() == srt::Error::InvalidArgument);

    auto mismatched = factory.create(loader);
    BOOST_REQUIRE(!mismatched);
    BOOST_CHECK(mismatched.error().code() == srt::Error::InvalidFormat);
}

#ifdef DSINFER_TEST_DRIVER_PLUGIN_PATH
BOOST_AUTO_TEST_CASE(test_LoadsOnnxDriverBundle) {
    ds::InferenceDriverFactory factory;
    const std::array<std::filesystem::path, 1> pluginPaths{DSINFER_TEST_DRIVER_PLUGIN_PATH};
    factory.setPluginPaths(pluginPaths);

    const auto backends = factory.backends();
    BOOST_REQUIRE_EQUAL(backends.size(), 1u);
    BOOST_CHECK_EQUAL(backends.front(), ds::Api::Onnx::API_NAME);

    auto loader = factory.find(ds::Api::Onnx::API_NAME);
    BOOST_REQUIRE(loader);
    const auto bundlePath = loader->filePath().parent_path();
    BOOST_CHECK(
        std::filesystem::equivalent(bundlePath, pluginPaths.front() / ds::Api::Onnx::API_NAME));

    auto result = factory.create(loader);
    BOOST_REQUIRE(result);
    auto driver = result.take();
    BOOST_CHECK_EQUAL(driver->backend(), "onnx");
    BOOST_CHECK(driver->extension() == nullptr);
    BOOST_CHECK(driver->createSession() == nullptr);

    auto invalidResult = factory.create(loader);
    BOOST_REQUIRE(invalidResult);
    auto invalidDriver = invalidResult.take();
    ds::Api::Onnx::DriverInitArgs invalidArgs;
    invalidArgs.runtimeApi = ds::Api::Onnx::RuntimeApi{};
    auto invalidInitialization = invalidDriver->initialize(invalidArgs);
    BOOST_REQUIRE(!invalidInitialization);
    BOOST_CHECK(invalidInitialization.error().code() == srt::Error::InvalidArgument);

    ds::Api::Onnx::DriverInitArgs initArgs;
    initArgs.runtimePath = bundlePath / "runtimes" / "onnx" / "default";
    BOOST_REQUIRE(driver->initialize(initArgs));
    auto duplicateInitialization = driver->initialize(initArgs);
    BOOST_REQUIRE(!duplicateInitialization);
    BOOST_CHECK(duplicateInitialization.error().code() == srt::Error::FileDuplicated);

    auto driverPointer = driver.get();
    srt::SynthUnit unit;
    BOOST_REQUIRE(unit.addRuntimeService(std::move(driver)));
    BOOST_CHECK(unit.runtimeService(ds::InferenceDriverPlugin::IID, ds::Api::Onnx::API_NAME) ==
                driverPointer);

    auto extension = driverPointer->extension()->as<ds::Api::Onnx::DriverExtension>();
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

    auto externalResult = factory.create(loader);
    BOOST_REQUIRE(externalResult);
    auto externalDriver = externalResult.take();
    ds::Api::Onnx::DriverInitArgs externalArgs;
    externalArgs.runtimeApi = extension->runtimeApi;
    BOOST_REQUIRE(externalDriver->initialize(externalArgs));
    auto externalExtension = externalDriver->extension()->as<ds::Api::Onnx::DriverExtension>();
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
    auto invalidModel = invalidModelSession->open(bundlePath / "plugin.json", openArgs);
    BOOST_REQUIRE(!invalidModel);
    BOOST_CHECK(invalidModel.error().code() == srt::Error::InvalidFormat);

    externalDriver.reset();
    BOOST_CHECK_EQUAL(externalSession->id(), 1);
    BOOST_CHECK(!externalSession->isOpen());

    auto coreMlResult = factory.create(loader);
    BOOST_REQUIRE(coreMlResult);
    auto coreMlDriver = coreMlResult.take();
    ds::Api::Onnx::DriverInitArgs coreMlArgs;
    coreMlArgs.runtimeApi = extension->runtimeApi;
    coreMlArgs.ep = ds::Api::Onnx::ExecutionProvider::CoreML;
    auto coreMlInitialization = coreMlDriver->initialize(coreMlArgs);
    BOOST_REQUIRE(!coreMlInitialization);
    BOOST_CHECK(coreMlInitialization.error().code() == srt::Error::FeatureNotSupported);
}

#  ifdef TEST_RESOURCE_DIRECTORY
BOOST_AUTO_TEST_CASE(test_RunsOnnxSessionsSynchronouslyAndAsynchronously) {
    const auto testCase = loadTestCase();
    const auto testOnnxModelPath = testResourceDirectory / testCase.modelPath;

    ds::InferenceDriverFactory factory;
    const std::array<std::filesystem::path, 1> pluginPaths{DSINFER_TEST_DRIVER_PLUGIN_PATH};
    factory.setPluginPaths(pluginPaths);

    auto loader = factory.find(ds::Api::Onnx::API_NAME);
    BOOST_REQUIRE(loader);
    const auto bundlePath = loader->filePath().parent_path();
    auto driverResult = factory.create(loader);
    BOOST_REQUIRE(driverResult);
    auto driver = driverResult.take();
    ds::Api::Onnx::DriverInitArgs initArgs;
    initArgs.runtimePath = bundlePath / "runtimes" / "onnx" / "default";
    BOOST_REQUIRE(driver->initialize(initArgs));

    ds::Api::Onnx::SessionOpenArgs openArgs;
    auto firstConcurrentSession = driver->createSession();
    auto secondConcurrentSession = driver->createSession();
    BOOST_REQUIRE(firstConcurrentSession);
    BOOST_REQUIRE(secondConcurrentSession);
    auto firstOpen = std::async(std::launch::async, [&] {
        return firstConcurrentSession->open(testOnnxModelPath, openArgs);
    });
    auto secondOpen = std::async(std::launch::async, [&] {
        return secondConcurrentSession->open(testOnnxModelPath, openArgs);
    });
    BOOST_REQUIRE(firstOpen.get());
    BOOST_REQUIRE(secondOpen.get());
    BOOST_REQUIRE(firstConcurrentSession->close());
    BOOST_REQUIRE(secondConcurrentSession->close());

    auto session = driver->createSession();
    BOOST_REQUIRE(session);
    BOOST_REQUIRE(session->open(testOnnxModelPath, openArgs));

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
    checkSessionResult(*syncResult.take()->as<ds::Api::Onnx::SessionResult>(),
                       *testCase.expectedResult);
    BOOST_CHECK(session->state() == srt::ITask::Succeeded);

    std::promise<srt::Expected<std::unique_ptr<srt::TaskResult>>> promise;
    auto future = promise.get_future();
    BOOST_REQUIRE(session->startAsync(makeSessionInput(), [&promise](auto result) mutable {
        promise.set_value(std::move(result));
    }));
    auto asyncResult = future.get();
    BOOST_REQUIRE(session->waitForFinished());
    BOOST_REQUIRE(asyncResult);
    checkSessionResult(*asyncResult.take()->as<ds::Api::Onnx::SessionResult>(),
                       *testCase.expectedResult);
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
    BOOST_REQUIRE(selfDestroyingSession->open(testOnnxModelPath, openArgs));
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
