#include <synthrt/Driver/onnx/OnnxTensor.h>

#include <cassert>
#include <cstring>
#include <limits>
#include <numeric>
#include <optional>
#include <string>

namespace srt::driver::onnx {

    static inline size_t getElementSizeFromOrtType(ONNXTensorElementDataType type) {
        switch (type) {
            case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT:
                return sizeof(float);
            case ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL:
                return sizeof(bool);
            case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64:
                return sizeof(int64_t);
            case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16:
                return sizeof(srt::core::Float16);
            case ONNX_TENSOR_ELEMENT_DATA_TYPE_DOUBLE:
                return sizeof(double);
            case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32:
                return sizeof(int32_t);
            case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8:
                return sizeof(uint8_t);
            default:
                return 0;
        }
    }

    static inline size_t getElementSize(srt::core::ITensor::DataType dataType) {
        switch (dataType) {
            case srt::core::ITensor::Float:
                return sizeof(float);
            case srt::core::ITensor::Int64:
                return sizeof(int64_t);
            case srt::core::ITensor::Bool:
                return sizeof(bool);
            case srt::core::ITensor::Float16:
                return sizeof(srt::core::Float16);
            case srt::core::ITensor::Float64:
                return sizeof(double);
            case srt::core::ITensor::Int32:
                return sizeof(int32_t);
            case srt::core::ITensor::UInt8:
                return sizeof(uint8_t);
            default:
                assert(false && "Unsupported data type");
                return 0;
        }
    }

    static inline std::optional<uint64_t>
        getElementCountFromShape(srt::core::ITensor::DataType dataType,
                                 const std::vector<int64_t> &shape) {
        if (shape.empty()) {
            return 1;
        }
        uint64_t totalElements = 1;

        const size_t elementSize = getElementSize(dataType);

        if (elementSize == 0) {
            return std::nullopt;
        }

        for (const auto dim : shape) {
            // Each dimension must be positive
            if (dim <= 0) {
                return std::nullopt;
            }

            // Check for multiplication overflow
            if (dim > std::numeric_limits<uint64_t>::max() / elementSize / totalElements) {
                return std::nullopt;
            }

            totalElements *= static_cast<uint64_t>(dim);
        }
        return totalElements;
    }

    static inline bool verifyShape(srt::core::ITensor::DataType dataType,
                                   const std::vector<int64_t> &shape, size_t dataSize) {
        if (shape.empty()) {
            return dataSize == getElementSize(dataType);
        }

        auto maybeTotalElements = getElementCountFromShape(dataType, shape);
        if (!maybeTotalElements.has_value()) {
            return false;
        }
        const uint64_t totalElements = maybeTotalElements.value();

        const size_t elementSize = getElementSize(dataType);

        if (elementSize == 0) {
            return false;
        }

        // Check whether total bytes match
        const uint64_t totalBytes = totalElements * static_cast<uint64_t>(elementSize);
        return totalBytes == static_cast<uint64_t>(dataSize);
    }

    srt::core::Expected<void>
        verify(srt::core::ITensor::DataType dataType, const std::vector<int64_t> &shape,
               size_t dataSize) {
        if (dataType == srt::core::ITensor::Undefined) {
            return srt::core::Error(srt::core::Error::InvalidArgument,
                                    "data type can not be Undefined");
        }
        if (!verifyShape(dataType, shape, dataSize)) {
            return srt::core::Error(srt::core::Error::InvalidArgument,
                                    "data size and shape mismatch");
        }
        return srt::core::Expected<void>();
    }

    void initOnnxRuntimeApi(const OrtApi* api) {
        Ort::InitApi(api);
    }

    OnnxTensor::OnnxTensor()
        : _value(nullptr), _dataType(Undefined), _elementSize(0), _bytesSize(0) {
    }

    OnnxTensor::OnnxTensor(OnnxTensor &&other) noexcept
        : _value(std::move(other._value)), _dataType(other._dataType),
          _shape(std::move(other._shape)), _elementSize(other._elementSize),
          _bytesSize(other._bytesSize) {
        other._dataType = Undefined;
        other._elementSize = 0;
        other._bytesSize = 0;
    }

    OnnxTensor &OnnxTensor::operator=(OnnxTensor &&other) noexcept {
        if (this != &other) {
            _value = std::move(other._value);
            _dataType = other._dataType;
            _shape = std::move(other._shape);
            _elementSize = other._elementSize;
            _bytesSize = other._bytesSize;

            other._dataType = Undefined;
            other._elementSize = 0;
            other._bytesSize = 0;
        }
        return *this;
    }

    OnnxTensor::~OnnxTensor() = default;

    srt::core::Expected<srt::core::NO<OnnxTensor>>
        OnnxTensor::create(DataType dataType, const std::vector<int64_t> &shape) {
        // Check if shape is valid
        auto maybeTotalElements = getElementCountFromShape(dataType, shape);
        if (!maybeTotalElements.has_value()) {
            return srt::core::Error(srt::core::Error::InvalidArgument, "invalid shape");
        }
        const uint64_t totalElements = maybeTotalElements.value();

        const size_t elementSize = getElementSize(dataType);
        if (elementSize == 0) {
            return srt::core::Error(srt::core::Error::InvalidArgument, "invalid data type");
        }

        // Create OnnxTensor object
        auto tensor = srt::core::NO<OnnxTensor>::create();
        if (!tensor) {
            return srt::core::Error(srt::core::Error::SessionError, "failed to create OnnxTensor");
        }
        ONNXTensorElementDataType onnxType = ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED;
        switch (dataType) {
            case Float:
                onnxType = ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT;
                break;
            case Bool:
                onnxType = ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL;
                break;
            case Int64:
                onnxType = ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64;
                break;
            case Float16:
                onnxType = ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16;
                break;
            case Float64:
                onnxType = ONNX_TENSOR_ELEMENT_DATA_TYPE_DOUBLE;
                break;
            case Int32:
                onnxType = ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32;
                break;
            case UInt8:
                onnxType = ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8;
                break;
            default:
                return srt::core::Error(srt::core::Error::InvalidArgument,
                                        "unsupported data type");
        }

        // Populate OnnxTensor metadata
        tensor->_dataType = dataType;
        tensor->_shape = shape;
        tensor->_elementSize = elementSize;
        tensor->_bytesSize = totalElements * elementSize;

        // Create underlying Ort::Value
        Ort::AllocatorWithDefaultOptions allocator{};
        tensor->_value = Ort::Value::CreateTensor(allocator, shape.data(), shape.size(), onnxType);
        if (!tensor->_value) {
            return srt::core::Error(srt::core::Error::SessionError,
                                    "failed to create Ort::Value tensor");
        }

        return tensor;
    }

    srt::core::Expected<srt::core::NO<OnnxTensor>>
        OnnxTensor::createFromRawView(DataType dataType, const std::vector<int64_t> &shape,
                                      const stdc::array_view<std::byte> &data) {

        auto exp = create(dataType, shape);
        if (!exp) {
            return exp.takeError();
        }
        auto tensor = exp.take();

        // Validate that data size matches the tensor buffer size to prevent buffer
        // overflow (data larger than buffer) or uninitialized memory (data smaller).
        if (data.size() != tensor->_bytesSize) {
            return srt::core::Error(
                srt::core::Error::InvalidArgument,
                "createFromRawView: data size (" + std::to_string(data.size()) +
                    ") does not match tensor buffer size (" +
                    std::to_string(tensor->_bytesSize) + ")");
        }

        // Copy data
        auto ortValueBuffer = static_cast<std::byte *>(tensor->_value.GetTensorMutableRawData());
        std::memcpy(ortValueBuffer, data.data(), data.size());

        return tensor;
    }

    srt::core::Expected<srt::core::NO<OnnxTensor>>
        OnnxTensor::createFromOrtValue(Ort::Value &&value) {
        auto tensor = srt::core::NO<OnnxTensor>::create();
        if (!tensor) {
            return srt::core::Error(srt::core::Error::SessionError, "failed to create OnnxTensor");
        }
        if (!value || !value.IsTensor()) {
            return srt::core::Error(srt::core::Error::InvalidArgument,
                                    "Ort::Value is null or not a tensor");
        }
        auto typeInfo = value.GetTensorTypeAndShapeInfo();
        auto ortType = typeInfo.GetElementType();

        tensor->_shape = typeInfo.GetShape();
        tensor->_elementSize = getElementSizeFromOrtType(ortType);
        tensor->_bytesSize = typeInfo.GetElementCount() * tensor->_elementSize;

        switch (ortType) {
            case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT:
                tensor->_dataType = Float;
                break;
            case ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL:
                tensor->_dataType = Bool;
                break;
            case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64:
                tensor->_dataType = Int64;
                break;
            case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16:
                tensor->_dataType = Float16;
                break;
            case ONNX_TENSOR_ELEMENT_DATA_TYPE_DOUBLE:
                tensor->_dataType = Float64;
                break;
            case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32:
                tensor->_dataType = Int32;
                break;
            case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8:
                tensor->_dataType = UInt8;
                break;
            default:
                return srt::core::Error(srt::core::Error::InvalidArgument,
                                        "unsupported data type");
        }

        tensor->_value = std::move(value);
        return tensor;
    }

    srt::core::Expected<srt::core::NO<OnnxTensor>>
        OnnxTensor::createFromTensor(const srt::core::NO<srt::core::ITensor> &tensor) {
        if (!tensor) {
            return srt::core::Error(srt::core::Error::InvalidArgument,
                                    "tensor must not be nullptr");
        }
        return createFromRawView(tensor->dataType(), tensor->shape(), tensor->rawView());
    }

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
        return BACKEND;
    }

    srt::core::ITensor::DataType OnnxTensor::dataType() const {
        return _dataType;
    }

    std::vector<int64_t> OnnxTensor::shape() const {
        return _shape;
    }

    std::vector<int64_t> OnnxTensor::strides() const {
        std::vector<int64_t> result(_shape.size());
        if (_shape.empty()) {
            return result;
        }
        // Contiguous, row-major: strides[i] = product of shape[i+1:]
        int64_t acc = 1;
        for (size_t i = _shape.size(); i-- > 0;) {
            result[i] = acc;
            acc *= _shape[i];
        }
        return result;
    }

    size_t OnnxTensor::byteSize() const {
        return _bytesSize;
    }

    size_t OnnxTensor::elementCount() const {
        if (_elementSize == 0) {
            return 0;
        }
        return _bytesSize / _elementSize;
    }

    size_t OnnxTensor::elementSize() const {
        return _elementSize;
    }

    const std::byte *OnnxTensor::rawData() const {
        if (!_value || !_value.IsTensor()) {
            return nullptr;
        }
        return static_cast<const std::byte *>(_value.GetTensorRawData());
    }

    std::byte *OnnxTensor::mutableRawData() {
        if (!_value || !_value.IsTensor()) {
            return nullptr;
        }
        return static_cast<std::byte *>(_value.GetTensorMutableRawData());
    }

    stdc::array_view<std::byte> OnnxTensor::rawView() const {
        if (!_value || !_value.IsTensor()) {
            return {};
        }
        return {rawData(), byteSize()};
    }

    srt::core::NO<srt::core::ITensor> OnnxTensor::clone() const {
        return createFromRawView(dataType(), shape(), rawView()).valueOr(nullptr);
    }

    srt::core::Expected<void> OnnxTensor::reshape(const std::vector<int64_t> &shape) {
        (void)shape;
        // OnnxTensor wraps an Ort::Value whose shape is managed by ONNX Runtime;
        // in-place reshape is not supported.
        return srt::core::Error(srt::core::Error::NotImplemented,
                                "OnnxTensor does not support in-place reshape");
    }

    bool OnnxTensor::isValid() const {
        return _value && _value.IsTensor() && _dataType != Undefined;
    }

}
