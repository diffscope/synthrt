#define BOOST_TEST_MODULE test_inference

#include <boost/test/unit_test.hpp>

#include <cmath>
#include <filesystem>
#include <memory>

#include <stdcorelib/path.h>
#include <stdcorelib/system.h>

#include <dsinfer/Api/Drivers/Onnx/OnnxDriverApi.h>
#include <dsinfer/Inference/InferenceDriver.h>
#include <dsinfer/Inference/InferenceDriverFactory.h>
#include <dsinfer/Support/ErrorCode.h>

#include "TestCaseLoader.h"

#define TST_CHECK_ASSIGN(var, expr)                                                                \
    do {                                                                                           \
        var = (expr);                                                                              \
        BOOST_TEST_CONTEXT("Expression: " #expr) {                                                 \
            BOOST_CHECK(var);                                                                      \
        }                                                                                          \
    } while (false)

#define TST_REQUIRE_ASSIGN(var, expr)                                                              \
    do {                                                                                           \
        var = (expr);                                                                              \
        BOOST_TEST_CONTEXT("Expression: " #expr) {                                                 \
            BOOST_REQUIRE(var);                                                                    \
        }                                                                                          \
    } while (false)

namespace fs = std::filesystem;

static constexpr float s_f32Tolerance = 1e-6f;

struct InferenceFixture {
    InferenceFixture() {
        auto appDir = stdc::system::application_directory();
        auto resDir = appDir / stdc::path::from_utf8(RESOURCE_DIR);
        modelDir = resDir / STDC_TSTR("models");
        caseDir = resDir / STDC_TSTR("cases");

        auto exp = initializeDriver();
        BOOST_TEST_MESSAGE("Driver initialization " +
                           std::string(exp ? "successful" : "failed: " + exp.error().message()));
        if (!exp) {
            BOOST_FAIL("Driver initialization failed: " + exp.error().message());
        }
    }

    ~InferenceFixture() = default;

    static srt::Expected<void> initializeDriver() {
        if (driver) {
            return {};
        }

        auto appDir = stdc::system::application_directory();
        auto defaultPluginDir =
            appDir.parent_path() / STDC_TSTR("lib") / STDC_TSTR("plugins") / STDC_TSTR("dsinfer");

        auto pluginPath = defaultPluginDir / STDC_TSTR("inferencedrivers");

        driverFactory.addPluginPath(pluginPath);
        auto driverResult = driverFactory.create(ds::Api::Onnx::API_NAME);
        if (!driverResult) {
            return driverResult.takeError();
        }
        auto onnxDriver = driverResult.take();

        const auto backend = onnxDriver->backend();
        constexpr auto expectedBackend = ds::Api::Onnx::API_NAME;
        if (backend != expectedBackend) {
            return srt::Error(ds::ErrorCode::DriverMismatch,
                              "inference driver does not implement the ONNX backend contract");
        }

        ds::Api::Onnx::DriverInitArgs onnxArgs;
        onnxArgs.ep = ds::Api::Onnx::ExecutionProvider::CPU;
        onnxArgs.runtimePath = pluginPath / STDC_TSTR("onnx") / STDC_TSTR("runtimes") /
                               STDC_TSTR("onnx") / STDC_TSTR("default");
        onnxArgs.deviceIndex = 0;

        auto exp = onnxDriver->initialize(onnxArgs);
        if (exp) {
            driver = std::move(onnxDriver);
            return {};
        }
        return exp.takeError();
    }

    static inline ds::InferenceDriverFactory driverFactory;
    static inline std::unique_ptr<ds::InferenceDriver> driver;
    fs::path modelDir;
    fs::path caseDir;
};

template <typename T>
static inline bool checkEqual(T x, T y, T epsilon = 1e-6) {
    if constexpr (std::is_floating_point_v<T>) {
        T diff = std::fabs(x - y);
        T norm = std::max({T(1), std::fabs(x), std::fabs(y)});
        return diff <= epsilon * norm;
    } else {
        return x == y;
    }
}

BOOST_FIXTURE_TEST_SUITE(InferenceTests, InferenceFixture)

BOOST_AUTO_TEST_CASE(initialize_driver) {
    auto exp = InferenceFixture::initializeDriver();
    auto initializeDriverOk = static_cast<bool>(exp);
    BOOST_TEST_MESSAGE("Initializing driver...");

    BOOST_CHECK(initializeDriverOk);
    if (initializeDriverOk) {
        BOOST_TEST_MESSAGE("Driver initialized successfully");
    } else {
        BOOST_FAIL("Driver initialization failed: " << exp.error().message());
    }
}

// What the driver loaded has to be legible from here, which is a module of its own. The onnxruntime
// headers are deliberately not linked in, so this checks that the extension arrives populated
// rather than what it says.
BOOST_AUTO_TEST_CASE(driver_extension) {
    BOOST_REQUIRE(InferenceFixture::driver);

    auto ext = InferenceFixture::driver->extension();
    BOOST_REQUIRE(ext != nullptr);
    BOOST_REQUIRE(ext->type() == ds::Api::Onnx::API_NAME);
    BOOST_REQUIRE(ext->version() == ds::Api::Onnx::API_VERSION);

    auto onnx = ext->as<ds::Api::Onnx::DriverExtension>();
    BOOST_CHECK(onnx->runtimeApi.ortApi != nullptr);
    BOOST_CHECK(onnx->runtimeApi.ortApiBase != nullptr);
    BOOST_CHECK(onnx->runtimeApi.ortApiVersion > 0);
    BOOST_CHECK(!onnx->runtimePath.empty());
    BOOST_CHECK(onnx->ep == ds::Api::Onnx::ExecutionProvider::CPU);
    BOOST_TEST_MESSAGE("ORT_API_VERSION the driver asked for: " << onnx->runtimeApi.ortApiVersion);
}

struct SessionResultValidator {
    explicit SessionResultValidator(const std::shared_ptr<test::TestCaseData> &testCaseData)
        : testCase(testCaseData) {
    }

    void operator()(srt::Expected<std::unique_ptr<srt::TaskResult>> result) const {
        BOOST_REQUIRE(result);
        auto sessionResult = result.take();
        BOOST_REQUIRE(sessionResult != nullptr);
        auto *onnxResult = sessionResult->as<ds::Api::Onnx::SessionResult>();

        // Verify outputs
        for (const auto &expectedOutputEntry : testCase->expectedResult->outputs) {
            const std::string &outputName = expectedOutputEntry.first;
            auto expectedTensor = expectedOutputEntry.second;

            // Check that output tensor exists
            auto it = onnxResult->outputs.find(outputName);
            BOOST_REQUIRE_MESSAGE(it != onnxResult->outputs.end(),
                                  "Output tensor '" << outputName << "' not found");
            auto outputTensor = it->second;
            BOOST_REQUIRE(outputTensor != nullptr);

            // Check data type matches
            auto actualDataType = outputTensor->dataType();
            auto expectedDataType = expectedTensor->dataType();
            BOOST_CHECK_EQUAL(actualDataType, expectedDataType);

            // Check shape matches
            const auto &outputShape = outputTensor->shape();
            const auto &expectedShape = expectedTensor->shape();
            BOOST_CHECK_EQUAL_COLLECTIONS(outputShape.begin(), outputShape.end(),
                                          expectedShape.begin(), expectedShape.end());

            // Check data element-wise
            auto outputRaw = outputTensor->rawView();
            auto expectedRaw = expectedTensor->rawView();
            size_t byteSize = outputTensor->byteSize();
            BOOST_CHECK_EQUAL(byteSize, expectedTensor->byteSize());

            switch (actualDataType) {
                case ds::ITensor::Float: {
                    auto actual = reinterpret_cast<const float *>(outputRaw.data());
                    auto expected = reinterpret_cast<const float *>(expectedRaw.data());
                    size_t count = byteSize / sizeof(float);
                    BOOST_TEST_MESSAGE("Floating point data comparison using tolerance "
                                       << s_f32Tolerance);
                    for (size_t i = 0; i < count; ++i) {
                        bool isEqual = checkEqual(actual[i], expected[i], s_f32Tolerance);
                        BOOST_CHECK_MESSAGE(isEqual, "Output: \"" << outputName << "\"; Index: "
                                                                  << i << "; Actual: " << actual[i]
                                                                  << "; Expected: " << expected[i]);
                    }
                    break;
                }
                default: {
                    // Compare raw data byte-by-byte
                    BOOST_TEST_MESSAGE("Comparing raw data byte-by-byte");
                    bool dataMatch =
                        std::memcmp(outputRaw.data(), expectedRaw.data(), byteSize) == 0;
                    BOOST_CHECK_MESSAGE(dataMatch, "Output data for '"
                                                       << outputName
                                                       << "' should match expected values");
                    break;
                }
            }
        }
    }

    std::shared_ptr<test::TestCaseData> testCase;
};

BOOST_AUTO_TEST_CASE(basic_model_input_and_output) {
    BOOST_REQUIRE(driver != nullptr);

    // Load test case data from JSON file using your loader
    const std::filesystem::path jsonTestFile = caseDir / "mixed_type_ops.json";
    std::shared_ptr<test::TestCaseData> testCase;
    try {
        BOOST_TEST_MESSAGE("Loading test data...");
        testCase = test::TestCaseLoader::load(jsonTestFile);
        BOOST_TEST_MESSAGE("Test data loaded");
    } catch (const test::TestCaseException &e) {
        BOOST_FAIL("Could not load test file: " << e.what());
    }

    // Create and open session
    auto session = driver->createSession();
    BOOST_REQUIRE(session != nullptr);

    ds::Api::Onnx::SessionOpenArgs sessionOpenArgs;
    sessionOpenArgs.useCpu = false;

    srt::Expected<void> sessionOpenRes =
        session->open(modelDir / testCase->meta.modelPath, sessionOpenArgs);
    srt::Error error = sessionOpenRes ? srt::Error::success() : sessionOpenRes.error();
    bool sessionOpenOk = false;
    TST_CHECK_ASSIGN(sessionOpenOk, bool(sessionOpenRes));
    BOOST_CHECK_EQUAL(session->isOpen(), sessionOpenOk);
    BOOST_CHECK_EQUAL(error.ok(), sessionOpenOk);
    BOOST_REQUIRE_MESSAGE(error.ok(), "Could NOT open session: " << error.message());

    // Start session using loaded inputs and requested outputs
    auto sessionStartRes = session->start(*testCase->sessionInput);
    error = sessionStartRes ? srt::Error::success() : sessionStartRes.error();
    bool sessionStartOk = false;
    TST_CHECK_ASSIGN(sessionStartOk, bool(sessionStartRes));
    BOOST_CHECK_EQUAL(error.ok(), sessionStartOk);
    BOOST_REQUIRE_MESSAGE(error.ok(), "Could NOT start session: " << error.message());

    // Validate result
    SessionResultValidator validator(testCase);
    validator(std::move(sessionStartRes));
}

BOOST_AUTO_TEST_CASE(basic_model_input_and_output_async) {
    BOOST_REQUIRE(driver != nullptr);

    // Load test case data from JSON file using your loader
    const std::filesystem::path jsonTestFile = caseDir / "mixed_type_ops.json";
    std::shared_ptr<test::TestCaseData> testCase;
    try {
        BOOST_TEST_MESSAGE("Loading test data...");
        testCase = test::TestCaseLoader::load(jsonTestFile);
        BOOST_TEST_MESSAGE("Test data loaded");
    } catch (const test::TestCaseException &e) {
        BOOST_FAIL("Could not load test file: " << e.what());
    }

    // Create and open session
    auto session = driver->createSession();
    BOOST_REQUIRE(session != nullptr);

    ds::Api::Onnx::SessionOpenArgs sessionOpenArgs;
    sessionOpenArgs.useCpu = false;

    srt::Expected<void> sessionOpenRes =
        session->open(modelDir / testCase->meta.modelPath, sessionOpenArgs);
    srt::Error error = sessionOpenRes ? srt::Error::success() : sessionOpenRes.error();
    bool sessionOpenOk = false;
    TST_CHECK_ASSIGN(sessionOpenOk, bool(sessionOpenRes));
    BOOST_CHECK_EQUAL(session->isOpen(), sessionOpenOk);
    BOOST_CHECK_EQUAL(error.ok(), sessionOpenOk);
    BOOST_REQUIRE_MESSAGE(error.ok(), "Could NOT open session: " << error.message());

    // Since we run the session async, we first construct the validator (callback)
    // The validator can be called like a function.
    SessionResultValidator validator(testCase);

    // Start session using loaded inputs and requested outputs
    srt::Expected<void> sessionStartRes = session->startAsync(testCase->sessionInput, validator);
    error = sessionStartRes ? srt::Error::success() : sessionStartRes.error();
    bool sessionStartOk = false;
    TST_CHECK_ASSIGN(sessionStartOk, bool(sessionStartRes));
    BOOST_CHECK_EQUAL(error.ok(), sessionStartOk);
    BOOST_REQUIRE_MESSAGE(error.ok(), "Could NOT start session: " << error.message());
    BOOST_REQUIRE(session->waitForFinished());
}

BOOST_AUTO_TEST_CASE(session_unopened) {
    BOOST_TEST_MESSAGE("Testing unopened session...");

    BOOST_REQUIRE(driver != nullptr);

    // Create session
    auto session = driver->createSession();
    BOOST_REQUIRE(session != nullptr);

    BOOST_CHECK_EQUAL(session->isOpen(), false);
    BOOST_CHECK(session->state() == srt::ITask::Idle);
}

BOOST_AUTO_TEST_SUITE_END()
