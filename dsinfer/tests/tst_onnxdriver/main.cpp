#define BOOST_TEST_MODULE tst_inference

#include <boost/test/unit_test.hpp>

#include <filesystem>
#include <fstream>
#include <numeric>
#include <cmath>

#include <stdcorelib/system.h>
#include <stdcorelib/path.h>

#include <synthrt/Core/SynthUnit.h>
#include <synthrt/Core/PackageRef.h>
#include <synthrt/Support/JSON.h>
#include <synthrt/Support/Logging.h>

#include <dsinfer/Inference/InferenceDriver.h>
#include <dsinfer/Inference/InferenceDriverPlugin.h>
#include <dsinfer/Api/Drivers/Onnx/OnnxDriverApi.h>
#include <dsinfer/Api/Singers/DiffSinger/1/DiffSingerApiL1.h>

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

static constexpr float f32_tolerance = 1e-6f;

struct InferenceFixture {
    InferenceFixture() {
        auto appDir = stdc::system::application_directory();
        auto resDir = appDir / stdc::path::from_utf8(RESOURCE_DIR);
        modelDir = resDir / _TSTR("models");
        caseDir = resDir / _TSTR("cases");

        auto exp = initializeSU();
        BOOST_TEST_MESSAGE("Driver initialization " +
                           std::string(exp ? "successful" : "failed: " + exp.error().message()));
        if (!exp) {
            BOOST_FAIL("Driver initialization failed: " + exp.error().message());
        }
    }

    ~InferenceFixture() = default;

    static srt::Expected<void> initializeSU() {
        if (driver) {
            return srt::Expected<void>(); // success
        }

        auto appDir = stdc::system::application_directory();
        auto defaultPluginDir =
            appDir.parent_path() / _TSTR("lib") / _TSTR("plugins") / _TSTR("dsinfer");

        auto pluginPath = defaultPluginDir / _TSTR("inferencedrivers");

        su.addPluginPath("org.openvpi.InferenceDriver", pluginPath);

        const char *pluginKey = "onnx";
        auto plugin = su.plugin<ds::InferenceDriverPlugin>(pluginKey);
        if (!plugin) {
            return srt::Error{
                srt::Error::FileNotFound,
                stdc::formatN("Could not load plugin \"%1\", path: %2", pluginKey, pluginPath),
            };
        }

        auto onnxDriver = plugin->create();

        if (!onnxDriver) {
            return srt::Error{srt::Error::SessionError, "Failed to create onnx driver"};
        }

        const auto arch = onnxDriver->arch();
        constexpr auto expectedArch = ds::Api::DiffSinger::L1::API_NAME;
        const bool isArchMatch = arch == expectedArch;

        const auto backend = onnxDriver->backend();
        constexpr auto expectedBackend = ds::Api::Onnx::API_NAME;
        const bool isBackendMatch = backend == expectedBackend;

        if (!isArchMatch || !isBackendMatch) {
            return srt::Error(
                srt::Error::SessionError,
                stdc::formatN(
                    R"(invalid driver: expected arch "%1", got "%2" (%3); expected backend "%4", got "%5" (%6))",
                    expectedArch, arch, (isArchMatch ? "match" : "MISMATCH"),
                    expectedBackend, backend, (isBackendMatch ? "match" : "MISMATCH")));
        }

        auto onnxArgs = srt::NO<ds::Api::Onnx::DriverInitArgs>::create();

        onnxArgs->ep = ds::Api::Onnx::CPUExecutionProvider;
        onnxArgs->runtimePath = plugin->path().parent_path() / _TSTR("runtimes");
        onnxArgs->deviceIndex = 0;

        auto exp = onnxDriver->initialize(onnxArgs);
        if (exp) {
            driver = std::move(onnxDriver);
            return srt::Expected<void>();
        }
        return exp.error();
    }


    static inline srt::SynthUnit su;
    static inline srt::NO<ds::InferenceDriver> driver;
    fs::path modelDir;
    fs::path caseDir;
};

template <typename T>
struct TensorDataTypeTraits {
    static_assert(sizeof(T) == 0, "Unsupported C++ type for TensorDataTypeTraits.");
};

template <>
struct TensorDataTypeTraits<float> {
    static constexpr ds::ITensor::DataType value = ds::ITensor::Float;
};

template <>
struct TensorDataTypeTraits<bool> {
    static constexpr ds::ITensor::DataType value = ds::ITensor::Bool;
};

template <>
struct TensorDataTypeTraits<int64_t> {
    static constexpr ds::ITensor::DataType value = ds::ITensor::Int64;
};

template <typename T>
constexpr ds::ITensor::DataType getTensorDataType() {
    return TensorDataTypeTraits<T>::value;
}

template <typename T>
static inline srt::NO<ds::Tensor> createTensorFromData(const std::vector<int64_t> &shape,
                                                       const std::vector<T> &data) {
    int64_t shapeSize =
        std::accumulate(shape.begin(), shape.end(), int64_t{1}, std::multiplies<>());
    if (shapeSize != static_cast<int64_t>(data.size())) {
        BOOST_FAIL("Size mismatch: shapeSize = " << shapeSize << ", data.size() = " << data.size());
        return {};
    }
    std::vector<uint8_t> rawBytes(data.size() * sizeof(T));
    std::memcpy(rawBytes.data(), data.data(), data.size() * sizeof(T));
    return srt::NO<ds::Tensor>::create(getTensorDataType<T>(), std::move(shape), rawBytes);
}

template <typename T>
static inline std::vector<int64_t> getVectorDataShape(const std::vector<T> &data) {
    return {int64_t{1}, static_cast<int64_t>(data.size())};
}

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
    auto exp = InferenceFixture::initializeSU();
    auto initialize_driver_ok = static_cast<bool>(exp);
    BOOST_TEST_MESSAGE("Initializing driver...");

    BOOST_CHECK(initialize_driver_ok);
    if (initialize_driver_ok) {
        BOOST_TEST_MESSAGE("Driver initialized successfully");
    } else {
        BOOST_FAIL("Driver initialization failed: " << exp.error().message());
    }
}

struct SessionResultValidator {
    explicit SessionResultValidator(const std::shared_ptr<test::TestCaseData> &testCaseData)
        : testCase(testCaseData) {
    }

    void operator()(const srt::NO<srt::TaskResult> &result_, const srt::Error &error) const {
        BOOST_REQUIRE(result_ != nullptr);

        // Cast to Onnx SessionResult
        auto result = result_.as<ds::Api::Onnx::SessionResult>();

        // Verify outputs
        for (const auto &expectedOutputEntry : testCase->expectedResult->outputs) {
            const std::string &outputName = expectedOutputEntry.first;
            auto expectedTensor = expectedOutputEntry.second;

            // Check that output tensor exists
            auto it = result->outputs.find(outputName);
            BOOST_REQUIRE_MESSAGE(it != result->outputs.end(),
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
            auto outputRaw = outputTensor->data();
            auto expectedRaw = expectedTensor->data();
            size_t byteSize = outputTensor->size();
            BOOST_CHECK_EQUAL(byteSize, expectedTensor->size());

            switch (actualDataType) {
                case ds::ITensor::Float: {
                    auto actual = reinterpret_cast<const float *>(outputRaw.data());
                    auto expected = reinterpret_cast<const float *>(expectedRaw.data());
                    size_t count = byteSize / sizeof(float);
                    BOOST_TEST_MESSAGE("Floating point data comparison using tolerance "
                                       << f32_tolerance);
                    for (size_t i = 0; i < count; ++i) {
                        bool isEqual = checkEqual(actual[i], expected[i], f32_tolerance);
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

    auto sessionOpenArgs = srt::NO<ds::Api::Onnx::SessionOpenArgs>::create();
    sessionOpenArgs->useCpu = false;

    srt::Expected<void> sessionOpenRes =
        session->open(modelDir / testCase->meta.model_path, sessionOpenArgs);
    srt::Error error = sessionOpenRes ? srt::Error::success() : sessionOpenRes.error();
    bool sessionOpenOk = false;
    TST_CHECK_ASSIGN(sessionOpenOk, bool(sessionOpenRes));
    BOOST_CHECK_EQUAL(session->isOpen(), sessionOpenOk);
    BOOST_CHECK_EQUAL(error.ok(), sessionOpenOk);
    BOOST_REQUIRE_MESSAGE(error.ok(), "Could NOT open session: " << error.message());

    // Start session using loaded inputs and requested outputs
    srt::Expected<void> sessionStartRes =
        session->start(testCase->sessionInput.as<srt::TaskStartInput>());
    error = sessionStartRes ? srt::Error::success() : sessionStartRes.error();
    bool sessionStartOk = false;
    TST_CHECK_ASSIGN(sessionStartOk, bool(sessionStartRes));
    BOOST_CHECK_EQUAL(error.ok(), sessionStartOk);
    BOOST_REQUIRE_MESSAGE(error.ok(), "Could NOT start session: " << error.message());

    // Retrieve result
    auto result_ = session->result();

    // Validate result
    SessionResultValidator validator(testCase);
    validator(result_, error);
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

    auto sessionOpenArgs = srt::NO<ds::Api::Onnx::SessionOpenArgs>::create();
    sessionOpenArgs->useCpu = false;

    srt::Expected<void> sessionOpenRes =
        session->open(modelDir / testCase->meta.model_path, sessionOpenArgs);
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
    srt::Expected<void> sessionStartRes =
        session->startAsync(testCase->sessionInput.as<srt::TaskStartInput>(), validator);
    error = sessionStartRes ? srt::Error::success() : sessionStartRes.error();
    bool sessionStartOk = false;
    TST_CHECK_ASSIGN(sessionStartOk, bool(sessionStartRes));
    BOOST_CHECK_EQUAL(error.ok(), sessionStartOk);
    BOOST_REQUIRE_MESSAGE(error.ok(), "Could NOT start session: " << error.message());
}

BOOST_AUTO_TEST_CASE(session_unopened) {
    BOOST_TEST_MESSAGE("Testing unopened session...");

    BOOST_REQUIRE(driver != nullptr);

    // Create session
    auto session = driver->createSession();
    BOOST_REQUIRE(session != nullptr);

    BOOST_CHECK_EQUAL(session->isOpen(), false);

    // Sessions, even unopened, should allocate the result object.
    BOOST_CHECK(session->result() != nullptr);
}

BOOST_AUTO_TEST_SUITE_END()
