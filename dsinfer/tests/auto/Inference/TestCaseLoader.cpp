#include "TestCaseLoader.h"

#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <stdcorelib/str.h>

#include <synthrt/Support/JSON.h>

#include <dsinfer/Core/Tensor.h>

namespace test {

    static ds::ITensor::DataType parseDataType(std::string_view text) {
        const auto type = stdc::to_lower(std::string(text));
        if (type == "float32" || type == "float") {
            return ds::ITensor::Float;
        }
        if (type == "int64") {
            return ds::ITensor::Int64;
        }
        if (type == "bool") {
            return ds::ITensor::Bool;
        }
        return ds::ITensor::Undefined;
    }

    static const srt::JsonValue &requiredField(const srt::JsonObject &object,
                                               std::string_view name) {
        const auto it = object.find(name);
        if (it == object.end()) {
            throw TestCaseError("missing required field " + std::string(name));
        }
        return it->second;
    }

    static std::string requiredString(const srt::JsonObject &object, std::string_view name) {
        const auto &value = requiredField(object, name);
        if (!value.isString() || value.toString().empty()) {
            throw TestCaseError("field " + std::string(name) + " must be a nonempty string");
        }
        return value.toString();
    }

    static std::shared_ptr<ds::ITensor> parseTensor(const srt::JsonValue &value) {
        if (!value.isObject()) {
            throw TestCaseError("tensor declaration must be an object");
        }
        const auto &object = value.toObject();
        const auto name = requiredString(object, "name");
        const auto dataType = parseDataType(requiredString(object, "dtype"));
        if (dataType == ds::ITensor::Undefined) {
            throw TestCaseError("tensor " + name + " uses an unsupported data type");
        }

        const auto &shapeValue = requiredField(object, "shape");
        if (!shapeValue.isArray()) {
            throw TestCaseError("tensor " + name + " shape must be an array");
        }
        std::vector<int64_t> shape;
        shape.reserve(shapeValue.toArray().size());
        for (const auto &dimension : shapeValue.toArray()) {
            if (!dimension.isInt()) {
                throw TestCaseError("tensor " + name + " dimensions must be integers");
            }
            shape.push_back(dimension.toInt());
        }

        const auto &dataValue = requiredField(object, "data");
        if (!dataValue.isArray()) {
            throw TestCaseError("tensor " + name + " data must be an array");
        }
        const auto &values = dataValue.toArray();
        ds::Tensor::Container data;

        switch (dataType) {
            case ds::ITensor::Float: {
                data.resize(values.size() * sizeof(float));
                auto output = reinterpret_cast<float *>(data.data());
                for (const auto &item : values) {
                    if (!item.isNumber()) {
                        throw TestCaseError("tensor " + name + " data must contain numbers");
                    }
                    *output++ = static_cast<float>(item.toDouble());
                }
                break;
            }
            case ds::ITensor::Int64: {
                data.resize(values.size() * sizeof(int64_t));
                auto output = reinterpret_cast<int64_t *>(data.data());
                for (const auto &item : values) {
                    if (!item.isInt()) {
                        throw TestCaseError("tensor " + name + " data must contain integers");
                    }
                    *output++ = item.toInt();
                }
                break;
            }
            case ds::ITensor::Bool:
                data.reserve(values.size());
                for (const auto &item : values) {
                    if (!item.isBool()) {
                        throw TestCaseError("tensor " + name + " data must contain booleans");
                    }
                    data.push_back(item.toBool() ? std::byte{1} : std::byte{0});
                }
                break;
            default:
                throw TestCaseError("tensor " + name + " uses an unsupported data type");
        }

        auto tensor = ds::Tensor::createFromRawData(dataType, shape, std::move(data));
        if (!tensor) {
            throw TestCaseError("invalid tensor " + name + ": " + tensor.error().message());
        }
        return tensor.take();
    }

    TestCaseData TestCaseLoader::load(const std::filesystem::path &path) {
        std::ifstream stream(path);
        if (!stream) {
            throw TestCaseError("failed to open test case " + path.string());
        }
        const std::string json{std::istreambuf_iterator<char>(stream),
                               std::istreambuf_iterator<char>()};

        stdc::json::ParseError parseError;
        const auto root = srt::JsonValue::fromJson(json, false, &parseError);
        if (parseError) {
            throw TestCaseError("failed to parse " + path.string() + ": " + parseError.message());
        }
        if (!root.isObject()) {
            throw TestCaseError("test case root must be an object");
        }
        const auto &object = root.toObject();

        TestCaseData result;
        result.id = requiredString(object, "test_id");
        result.description = requiredString(object, "description");
        result.modelPath = requiredString(object, "model_path");
        result.input = std::make_shared<ds::Api::Onnx::SessionStartInput>();
        result.expectedResult = std::make_shared<ds::Api::Onnx::SessionResult>();

        const auto &inputs = requiredField(object, "inputs");
        if (!inputs.isArray() || inputs.toArray().empty()) {
            throw TestCaseError("inputs must be a nonempty array");
        }
        for (const auto &input : inputs.toArray()) {
            if (!input.isObject()) {
                throw TestCaseError("input tensor declaration must be an object");
            }
            const auto name = requiredString(input.toObject(), "name");
            if (!result.input->inputs.emplace(name, parseTensor(input)).second) {
                throw TestCaseError("duplicate input tensor " + name);
            }
        }

        const auto &outputs = requiredField(object, "expected_outputs");
        if (!outputs.isArray() || outputs.toArray().empty()) {
            throw TestCaseError("expected_outputs must be a nonempty array");
        }
        for (const auto &output : outputs.toArray()) {
            if (!output.isObject()) {
                throw TestCaseError("output tensor declaration must be an object");
            }
            const auto name = requiredString(output.toObject(), "name");
            result.input->outputs.insert(name);
            if (!result.expectedResult->outputs.emplace(name, parseTensor(output)).second) {
                throw TestCaseError("duplicate output tensor " + name);
            }
        }

        return result;
    }

}
