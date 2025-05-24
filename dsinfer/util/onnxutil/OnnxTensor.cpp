#include "OnnxTensor.h"

#include <numeric>

namespace ds {

    static inline size_t getElementSizeFromOrtType(ONNXTensorElementDataType type) {
        switch (type) {
            case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT:
                return sizeof(float);
            case ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL:
                return sizeof(bool);
            case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64:
                return sizeof(int64_t);
            default:
                return 0;
        }
    }

    OnnxTensor::OnnxTensor() : _value(nullptr) {
    }

    OnnxTensor::OnnxTensor(DataType dataType, const std::vector<int64_t> &shape,
                       const std::vector<uint8_t> &data) : _value(nullptr) {
        ONNXTensorElementDataType onnxType;
        size_t elementSize = 0;

        switch (dataType) {
            case DataType::Float:
                onnxType = ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT;
                elementSize = sizeof(float);
                break;
            case DataType::Bool:
                onnxType = ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL;
                elementSize = sizeof(bool);
                break;
            case DataType::Int64:
                onnxType = ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64;
                elementSize = sizeof(int64_t);
                break;
            default:
                _value = Ort::Value(nullptr);
                return;  // unsupported type, just return
        }

        // Calculate total number of elements
        int64_t totalElements = std::accumulate(shape.begin(), shape.end(), int64_t{1}, std::multiplies<>());

        // Check if data size matches shape * element size
        if (totalElements * elementSize != static_cast<int64_t>(data.size())) {
            _value = Ort::Value(nullptr);
            return;
        }

        // Create tensor
        Ort::AllocatorWithDefaultOptions allocator{};
        _value = Ort::Value::CreateTensor(allocator, shape.data(), shape.size(), onnxType);
        if (!_value) {
            return;
        }

        // Copy data
        void *ortValueBuffer = _value.GetTensorMutableRawData();
        std::memcpy(ortValueBuffer, data.data(), data.size());
    }

    OnnxTensor::OnnxTensor(const Tensor &tensor)
        : OnnxTensor(tensor.dataType(), tensor.shape(), tensor.data()) {
    }

    OnnxTensor::OnnxTensor(Ort::Value &&value)
        : _value(std::move(value)) {
    }

    OnnxTensor::OnnxTensor(OnnxTensor &&other) noexcept
        : _value(std::move(other._value)) {
    }

    OnnxTensor& OnnxTensor::operator=(OnnxTensor &&other) noexcept {
        if (this != &other) {
            _value = std::move(other._value);
        }
        return *this;
    }

    OnnxTensor::~OnnxTensor() = default;

    Ort::Value OnnxTensor::takeOrtValue(Ort::Value &&value) {
        Ort::Value valueToRelease = std::move(_value);
        _value = std::move(value);
        return valueToRelease;
    }

    Ort::Value OnnxTensor::releaseOrtValue() {
        Ort::Value valueToRelease = std::move(_value);
        _value = Ort::Value(nullptr);
        return valueToRelease;
    }

    Ort::Value *OnnxTensor::valuePtr() {
        return &_value;
    }

    const Ort::Value *OnnxTensor::valuePtr() const {
        return &_value;
    }

    std::string OnnxTensor::backend() const {
        return "onnx";
    }

    bool OnnxTensor::isValid() const {
        return _value && _value.IsTensor();
    }

    // Trailing return type allows using DataType without qualification.
    // Otherwise, we’d need to write ITensor::DataType here.
    auto OnnxTensor::dataType() const -> DataType {
        if (!isValid()) {
            return DataType::Undefined;
        }

        auto onnxType = _value.GetTensorTypeAndShapeInfo().GetElementType();

        switch (onnxType) {
            case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT:
                return DataType::Float;
            case ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL:
                return DataType::Bool;
            case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64:
                return DataType::Int64;
            default:
                return DataType::Undefined;
        }
    }

    std::vector<int64_t> OnnxTensor::shape() const {
        if (!isValid()) return {};
        return _value.GetTensorTypeAndShapeInfo().GetShape();
    }

    size_t OnnxTensor::size() const {
        if (!isValid()) {
            return 0;
        }
        auto info = _value.GetTensorTypeAndShapeInfo();
        return info.GetElementCount() * getElementSizeFromOrtType(info.GetElementType());
    }

    std::vector<uint8_t> OnnxTensor::data() const {
        std::vector<uint8_t> result;
        if (!isValid()) {
            return result;
        }

        auto type = dataType();
        auto elementCount = size();

        const void* rawData = _value.GetTensorRawData();
        if (!rawData) {
            return result;
        }

        size_t byteSize = 0;
        switch (type) {
            case DataType::Float:
                byteSize = elementCount * sizeof(float);
                break;
            case DataType::Bool:
                byteSize = elementCount * sizeof(bool);
                break;
            case DataType::Int64:
                byteSize = elementCount * sizeof(int64_t);
                break;
            default:
                return result;
        }

        result.resize(byteSize);
        std::memcpy(result.data(), rawData, byteSize);
        return result;
    }


    srt::NO<ITensor> OnnxTensor::clone() const {
        if (!isValid()) {
            return nullptr;
        }

        // Create a new OnnxTensor from current data
        auto cloned = srt::NO<OnnxTensor>::create(dataType(), shape(), data());
        return cloned.as<ITensor>();
    }
}