#include "TestCaseLoader.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>

#include <stdcorelib/path.h>
#include <stdcorelib/str.h>
#include <synthrt/Support/JSON.h>

namespace test {

    TestCaseData::TestCaseData()
        : sessionInput(srt::NO<ds::Api::Onnx::SessionStartInput>::create()),
          expectedResult(srt::NO<ds::Api::Onnx::SessionResult>::create()) {
    }

    TestCaseData::TestCaseData(TestCaseMeta meta, srt::NO<ds::Api::Onnx::SessionStartInput> input,
                               srt::NO<ds::Api::Onnx::SessionResult> expectedResult)
        : meta(std::move(meta)), sessionInput(std::move(input)),
          expectedResult(std::move(expectedResult)) {
        if (!sessionInput) {
            sessionInput = srt::NO<ds::Api::Onnx::SessionStartInput>::create();
        }
        if (!expectedResult) {
            expectedResult = srt::NO<ds::Api::Onnx::SessionResult>::create();
        }
    }

    static ds::ITensor::DataType parse_data_type(const std::string_view str) {
        auto s = stdc::to_lower(std::string(str));
        if (s == "float32" || s == "float") {
            return ds::ITensor::Float;
        }
        if (s == "int64") {
            return ds::ITensor::Int64;
        }
        if (s == "bool") {
            return ds::ITensor::Bool;
        }
        return ds::ITensor::Undefined;
    }

    static srt::NO<ds::ITensor> parse_tensor(const srt::JsonValue &root) {
        // JSON: first ensure the input is an object
        if (!root.isObject()) {
            throw TestCaseException("Failed to parse JSON: invalid JSON object");
        }
        // JSON: parse name field. Must exist, and be a non-empty string
        const auto &nameField = root["name"];
        if (!nameField.isString()) {
            throw TestCaseException("Failed to parse JSON: name field must be a string");
        }
        auto name = root["name"].toString();
        if (name.empty()) {
            throw TestCaseException("Failed to parse JSON: tensor name is empty");
        }

        // JSON: parse dtype field. Must exist, and be valid values.
        const auto &dtypeField = root["dtype"];
        if (!dtypeField.isString()) {
            throw TestCaseException("Failed to parse JSON: dtype field must be a string");
        }
        auto dtype = parse_data_type(root["dtype"].toString());

        if (dtype == ds::ITensor::Undefined) {
            throw TestCaseException("Failed to parse JSON: unsupported data type");
        }

        // JSON: parse shape field. Must exist, and be an 1D array of numbers.
        const auto &shapeField = root["shape"];
        if (!shapeField.isArray()) {
            throw TestCaseException("Failed to parse JSON: shape field must be an array");
        }
        std::vector<int64_t> shape;
        const auto &shapeArray = shapeField.toArray();
        shape.reserve(shapeArray.size());
        for (const auto &v : std::as_const(shapeArray)) {
            if (!(v.type() == srt::JsonValue::Double || v.type() == srt::JsonValue::Int ||
                  v.type() == srt::JsonValue::UInt)) {
                throw TestCaseException(
                    "Failed to parse JSON: shape array elements must be of numeric type");
            }
            shape.push_back(v.toInt());
        }

        // JSON: parse shape field. Must exist, and be an 1D array of numbers.
        // (for multidimensional data, they are expressed in the row-major form)
        std::vector<uint8_t> raw;
        const auto &dataField = root["data"];
        if (!dataField.isArray()) {
            throw TestCaseException("Failed to parse JSON: data field must be an array");
        }
        const auto &values = dataField.toArray();

        switch (dtype) {
            case ds::ITensor::Float: {
                raw.resize(values.size() * sizeof(float));
                auto *buf = reinterpret_cast<float *>(raw.data());
                for (const auto &v : values) {
                    if (!(v.type() == srt::JsonValue::Double || v.type() == srt::JsonValue::Int ||
                          v.type() == srt::JsonValue::UInt)) {
                        throw TestCaseException("Failed to parse JSON: expected float type");
                    }
                    *buf++ = static_cast<float>(v.toDouble());
                }
                break;
            }
            case ds::ITensor::Int64: {
                raw.resize(values.size() * sizeof(int64_t));
                auto *buf = reinterpret_cast<int64_t *>(raw.data());
                for (const auto &v : values) {
                    if (!(v.type() == srt::JsonValue::Double || v.type() == srt::JsonValue::Int ||
                          v.type() == srt::JsonValue::UInt)) {
                        throw TestCaseException("Failed to parse JSON: expected integer type");
                    }
                    *buf++ = static_cast<int64_t>(v.toInt());
                }
                break;
            }
            case ds::ITensor::Bool: {
                raw.reserve(values.size());
                for (const auto &v : values) {
                    if (!v.isBool()) {
                        throw TestCaseException("Failed to parse JSON: expected boolean type");
                    }
                    raw.push_back(v.toBool() ? 1 : 0);
                }
                break;
            }
            default:
                throw TestCaseException("Failed to parse JSON: unsupported data type");
        }

        return srt::NO<ds::Tensor>::create(dtype, std::move(shape), std::move(raw))
            .as<ds::ITensor>();
    }

    std::shared_ptr<TestCaseData> TestCaseLoader::load(const std::filesystem::path &jsonPath) {
        std::ifstream in(jsonPath);
        if (!in)
            throw TestCaseException("Failed to open JSON file: " + jsonPath.string());

        std::string json((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

        std::string error;
        auto root = srt::JsonValue::fromJson(json, true, &error);
        if (root.isUndefined())
            throw TestCaseException("JSON parse error in file '" + jsonPath.string() +
                                    "': " + error);

        auto caseData = std::make_shared<TestCaseData>();
        caseData->meta.test_id = root["test_id"].toString();
        caseData->meta.description = root["description"].toString();
        caseData->meta.model_path = stdc::path::from_utf8(root["model_path"].toString());

        auto &inputMap = caseData->sessionInput->inputs;
        const auto jsonInputs = root["inputs"].toArray();
        if (jsonInputs.empty())
            throw TestCaseException("No inputs found in test case JSON: " + jsonPath.string());

        for (const auto &jsonInput : jsonInputs) {
            if (!jsonInput["name"].isString())
                throw TestCaseException("Input missing 'name' field in test case JSON: " +
                                        jsonPath.string());

            std::string name = jsonInput["name"].toString();
            inputMap[name] = parse_tensor(jsonInput);
        }

        const auto jsonExpectedOutputs = root["expected_outputs"].toArray();
        if (jsonExpectedOutputs.empty())
            throw TestCaseException("No expected_outputs found in test case JSON: " +
                                    jsonPath.string());

        for (const auto &jsonExpectedOutput : jsonExpectedOutputs) {
            if (!jsonExpectedOutput["name"].isString())
                throw TestCaseException("Expected output missing 'name' field in test case JSON: " +
                                        jsonPath.string());

            std::string name = jsonExpectedOutput["name"].toString();

            // Insert output name for session output fetching
            caseData->sessionInput->outputs.insert(name);

            // Parse and store expected output tensor
            caseData->expectedResult->outputs[name] = parse_tensor(jsonExpectedOutput);
        }

        return caseData;
    }

} // namespace test
