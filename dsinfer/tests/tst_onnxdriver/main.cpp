#define BOOST_TEST_MODULE tst_inference


#include <boost/test/unit_test.hpp>

#include <filesystem>
#include <fstream>
#include <numeric>

#include <stdcorelib/system.h>
#include <stdcorelib/path.h>

#include <synthrt/Core/SynthUnit.h>
#include <synthrt/Core/PackageRef.h>
#include <synthrt/Support/JSON.h>
#include <synthrt/Support/Logging.h>

#include <dsinfer/Inference/InferenceDriver.h>
#include <dsinfer/Inference/InferenceDriverPlugin.h>
#include <dsinfer/Api/Drivers/Onnx/OnnxDriverApi.h>

#define TST_CHECK_ASSIGN(var, expr)                        \
    do {                                                   \
        var = (expr);                                      \
        BOOST_TEST_CONTEXT("Expression: " #expr) {         \
            BOOST_CHECK(var);                              \
        }                                                  \
    } while (false)

#define TST_REQUIRE_ASSIGN(var, expr)                      \
    do {                                                   \
        var = (expr);                                      \
        BOOST_TEST_CONTEXT("Expression: " #expr) {         \
            BOOST_REQUIRE(var);                            \
        }                                                  \
    } while (false)

namespace fs = std::filesystem;

static constexpr float f32_tolerance = 1e-6f;

struct InferenceFixture {
    InferenceFixture() {
        auto appDir = stdc::system::application_directory();
        auto resDir = appDir / stdc::path::from_utf8(RESOURCE_DIR);
        modelDir = resDir / _TSTR("models");

        auto error = initializeSU();
        BOOST_TEST_MESSAGE("Driver initialization " + std::string(error.ok() ? "successful" : "failed: " + error.message()));
        if (!error.ok()) {
            BOOST_FAIL("Driver initialization failed: " + error.message());
        }
    }

    ~InferenceFixture() = default;

    static srt::Error initializeSU() {
        if (driver) {
            return {}; // success
        }

        auto appDir = stdc::system::application_directory();
        auto defaultPluginDir =
            appDir.parent_path() / _TSTR("lib") / _TSTR("plugins") / _TSTR("dsinfer");

        auto pluginPath = defaultPluginDir / _TSTR("inferencedrivers");

        su.addPluginPath("org.openvpi.InferenceDriver", pluginPath);

        const char *pluginKey = "onnx";
        auto plugin = su.plugin<ds::InferenceDriverPlugin>(pluginKey);
        if (!plugin) {
            return {srt::Error::FileNotFound,
                stdc::formatN("Could not load plugin \"%1\", path: %2", pluginKey, pluginPath)};
        }

        auto onnxDriver = plugin->create();
        auto onnxArgs = srt::NO<ds::Api::Onnx::DriverInitArgs>::create();

        onnxArgs->ep = ds::Api::Onnx::CPUExecutionProvider;
        onnxArgs->runtimePath = plugin->path().parent_path() / _TSTR("runtimes");
        onnxArgs->deviceIndex = 0;

        srt::Error error;
        bool ok = onnxDriver->initialize(onnxArgs, &error);

        if (ok) {
            driver = std::move(onnxDriver);
        }
        return error;
    }


    static inline srt::SynthUnit su;
    static inline srt::NO<ds::InferenceDriver> driver;
    fs::path modelDir;
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
static inline srt::NO<ds::Tensor> createTensorFromData(const std::vector<int64_t> &shape, const std::vector<T> &data) {
    int64_t shapeSize = std::accumulate(shape.begin(), shape.end(), int64_t{1}, std::multiplies<>());
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

BOOST_FIXTURE_TEST_SUITE(InferenceTests, InferenceFixture)

BOOST_AUTO_TEST_CASE(initialize_driver)
{
    auto error = InferenceFixture::initializeSU();
    BOOST_TEST_MESSAGE("Initializing driver...");

    BOOST_CHECK_MESSAGE(error.ok(), error.message());
    if (error.ok()) {
        BOOST_TEST_MESSAGE("Driver initialized successfully");
    }
}

BOOST_AUTO_TEST_CASE(basic_model_input_and_output)
{
    BOOST_REQUIRE(driver != nullptr);

    auto session = driver->createSession();
    BOOST_REQUIRE(session != nullptr);

    auto sessionOpenArgs = srt::NO<ds::Api::Onnx::SessionOpenArgs>::create();
    sessionOpenArgs->useCpu = false;

    auto modelPath = modelDir / "vector_add.onnx";

    srt::Error error;

    bool sessionOpenOk = false;
    TST_CHECK_ASSIGN(sessionOpenOk, session->open(modelPath, sessionOpenArgs, &error));
    BOOST_CHECK_EQUAL(error.ok(), sessionOpenOk);
    BOOST_REQUIRE(error.ok(), "Could NOT open session: " << error.message());

    auto sessionStartInput = srt::NO<ds::Api::Onnx::SessionStartInput>::create();

    auto input1data = std::vector<float>{1.0f, 2.0f, 3.0f, 4.0f};
    auto input2data = std::vector<float>{10.5f, 20.5f, 30.5f, 40.5f};

    auto input1shape = getVectorDataShape(input1data);
    auto input2shape = getVectorDataShape(input2data);

    auto input1tensor = createTensorFromData<float>(input1shape, input1data);
    auto input2tensor = createTensorFromData<float>(input2shape, input2data);
    BOOST_CHECK(input1tensor != nullptr);
    BOOST_CHECK(input2tensor != nullptr);

    if (input1tensor == nullptr || input2tensor == nullptr) {
        BOOST_FAIL("Could not create input tensors!");
    }

    sessionStartInput->inputs["input1"] = input1tensor;
    sessionStartInput->inputs["input2"] = input2tensor;
    sessionStartInput->outputs.emplace("output");

    bool sessionStartOk = false;
    TST_CHECK_ASSIGN(sessionStartOk, session->start(sessionStartInput.as<srt::TaskStartInput>(), &error));
    BOOST_CHECK_EQUAL(error.ok(), sessionStartOk);
    BOOST_REQUIRE(error.ok(), "Could NOT start session: " << error.message());

    auto result_ = session->result();

    std::vector<float> expectedOutput = {11.5f, 22.5f, 33.5f, 44.5f};
    auto expectedOutputElementCount = static_cast<int64_t>(expectedOutput.size());
    auto expectedOutputShape = getVectorDataShape(expectedOutput);

    BOOST_CHECK_EQUAL(result_->objectName(), ds::Api::Onnx::API_NAME);
    auto result = result_.as<ds::Api::Onnx::SessionResult>();
    // 1. Check output tensor exists
    auto it = result->outputs.find("output");
    BOOST_REQUIRE_MESSAGE(it != result->outputs.end(), "Output tensor 'output' not found");

    auto outputTensor = it->second;
    BOOST_REQUIRE(outputTensor != nullptr);

    // 2. Check output shape size matches expected
    const auto &outputShape = outputTensor->shape();
    int64_t outputElementCount = std::accumulate(outputShape.begin(), outputShape.end(), int64_t{1}, std::multiplies<>());
    BOOST_CHECK_EQUAL(outputElementCount, expectedOutputElementCount);

    // 3. Get raw bytes and reinterpret as float
    BOOST_CHECK_EQUAL(outputTensor->dataType(), ds::ITensor::Float);

    const std::vector<uint8_t> &rawData = outputTensor->data();

    BOOST_REQUIRE_EQUAL(rawData.size(), (expectedOutput.size() * sizeof(float)));

    const float* outputData = reinterpret_cast<const float*>(rawData.data());

    // 4. Compare element-wise with expected output
    for (size_t i = 0; i < expectedOutput.size(); ++i) {
        BOOST_CHECK_CLOSE(outputData[i], expectedOutput[i], f32_tolerance);
    }
}

BOOST_AUTO_TEST_SUITE_END()
