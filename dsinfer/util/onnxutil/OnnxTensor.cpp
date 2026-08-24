#include "OnnxTensor.h"

#include <cstring>
#include <limits>
#include <optional>
#include <utility>

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

    static inline size_t getElementSize(ITensor::DataType dataType) {
        switch (dataType) {
            case ITensor::Float:
                return sizeof(float);
            case ITensor::Int64:
                return sizeof(int64_t);
            case ITensor::Bool:
                return sizeof(bool);
            default:
                return 0;
        }
    }

    static inline std::optional<uint64_t>
        getElementCountFromShape(ITensor::DataType dataType, const std::vector<int64_t> &shape) {
        if (shape.empty()) {
            return 1;
        }
        uint64_t totalElements = 1;

        const size_t elementSize = getElementSize(dataType);

        if (elementSize == 0) {
            return std::nullopt;
        }

        for (const auto dim : shape) {
            // Dynamic and zero sized dimensions cannot describe allocated tensor storage.
            if (dim <= 0) {
                return std::nullopt;
            }

            // Include element size in the bound so the resulting byte count also fits.
            if (dim > std::numeric_limits<uint64_t>::max() / elementSize / totalElements) {
                return std::nullopt;
            }

            totalElements *= static_cast<uint64_t>(dim);
        }
        return totalElements;
    }

    static inline bool verifyShape(ITensor::DataType dataType, const std::vector<int64_t> &shape,
                                   size_t dataSize) {
        if (shape.empty()) {
            const auto elementSize = getElementSize(dataType);
            return elementSize != 0 && dataSize == elementSize;
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

        const uint64_t totalBytes = totalElements * static_cast<uint64_t>(elementSize);
        return totalBytes == static_cast<uint64_t>(dataSize);
    }

    static srt::Expected<void> verify(ITensor::DataType dataType, const std::vector<int64_t> &shape,
                                      size_t dataSize) {
        if (dataType == ITensor::Undefined) {
            return srt::Error(srt::Error::InvalidArgument, "data type cannot be Undefined");
        }
        if (!verifyShape(dataType, shape, dataSize)) {
            return srt::Error(srt::Error::InvalidArgument, "data size and shape mismatch");
        }
        return srt::Expected<void>();
    }

    OnnxTensor::OnnxTensor()
        : m_value(nullptr), m_dataType(Undefined), m_elementSize(0), m_byteSize(0) {
    }

    OnnxTensor::OnnxTensor(OnnxTensor &&other) noexcept
        : m_value(std::move(other.m_value)), m_dataType(other.m_dataType),
          m_shape(std::move(other.m_shape)), m_elementSize(other.m_elementSize),
          m_byteSize(other.m_byteSize) {
        other.m_dataType = Undefined;
        other.m_elementSize = 0;
        other.m_byteSize = 0;
    }

    OnnxTensor &OnnxTensor::operator=(OnnxTensor &&other) noexcept {
        if (this != &other) {
            m_value = std::move(other.m_value);
            m_dataType = other.m_dataType;
            m_shape = std::move(other.m_shape);
            m_elementSize = other.m_elementSize;
            m_byteSize = other.m_byteSize;

            other.m_dataType = Undefined;
            other.m_elementSize = 0;
            other.m_byteSize = 0;
        }
        return *this;
    }

    OnnxTensor::~OnnxTensor() = default;

    srt::Expected<std::shared_ptr<OnnxTensor>>
        OnnxTensor::create(DataType dataType, const std::vector<int64_t> &shape) {
        auto maybeTotalElements = getElementCountFromShape(dataType, shape);
        if (!maybeTotalElements.has_value()) {
            return srt::Error(srt::Error::InvalidArgument, "invalid shape");
        }
        const uint64_t totalElements = maybeTotalElements.value();

        const size_t elementSize = getElementSize(dataType);
        if (elementSize == 0) {
            return srt::Error(srt::Error::InvalidArgument, "invalid data type");
        }

        auto tensor = std::make_shared<OnnxTensor>();
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
            default:
                return srt::Error(srt::Error::InvalidArgument, "unsupported data type");
        }

        try {
            Ort::AllocatorWithDefaultOptions allocator;
            tensor->m_value =
                Ort::Value::CreateTensor(allocator, shape.data(), shape.size(), onnxType);
        } catch (const Ort::Exception &error) {
            return srt::Error(ds::ErrorCode::ProcessingFailed, error.what());
        }

        tensor->m_dataType = dataType;
        tensor->m_shape = shape;
        tensor->m_elementSize = elementSize;
        tensor->m_byteSize = totalElements * elementSize;
        return tensor;
    }

    srt::Expected<std::shared_ptr<OnnxTensor>>
        OnnxTensor::createFromRawView(DataType dataType, const std::vector<int64_t> &shape,
                                      const stdc::array_view<std::byte> &data) {
        if (!data.empty() && !data.data()) {
            return srt::Error(srt::Error::InvalidArgument, "tensor storage must not be null");
        }
        if (auto verified = verify(dataType, shape, data.size()); !verified) {
            return verified.takeError();
        }

        auto tensorResult = create(dataType, shape);
        if (!tensorResult) {
            return tensorResult.takeError();
        }
        auto tensor = tensorResult.take();

        if (!data.empty()) {
            auto ortValueBuffer =
                static_cast<std::byte *>(tensor->m_value.GetTensorMutableRawData());
            std::memcpy(ortValueBuffer, data.data(), data.size());
        }

        return tensor;
    }

    srt::Expected<std::shared_ptr<OnnxTensor>> OnnxTensor::createFromOrtValue(Ort::Value &&value) {
        if (!value || !value.IsTensor()) {
            return srt::Error(srt::Error::InvalidArgument, "Ort::Value is null or not a tensor");
        }
        try {
            auto tensor = std::make_shared<OnnxTensor>();
            auto typeInfo = value.GetTensorTypeAndShapeInfo();
            const auto ortType = typeInfo.GetElementType();

            tensor->m_shape = typeInfo.GetShape();
            tensor->m_elementSize = getElementSizeFromOrtType(ortType);
            tensor->m_byteSize = typeInfo.GetElementCount() * tensor->m_elementSize;

            switch (ortType) {
                case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT:
                    tensor->m_dataType = Float;
                    break;
                case ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL:
                    tensor->m_dataType = Bool;
                    break;
                case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64:
                    tensor->m_dataType = Int64;
                    break;
                default:
                    return srt::Error(srt::Error::InvalidArgument, "unsupported data type");
            }

            tensor->m_value = std::move(value);
            return tensor;
        } catch (const Ort::Exception &error) {
            return srt::Error(srt::Error::InvalidArgument, error.what());
        }
    }

    srt::Expected<std::shared_ptr<OnnxTensor>>
        OnnxTensor::createFromTensor(const std::shared_ptr<ITensor> &tensor) {
        if (!tensor) {
            return srt::Error(srt::Error::InvalidArgument, "tensor must not be nullptr");
        }
        if (tensor->byteSize() != 0 && !tensor->rawData()) {
            return srt::Error(srt::Error::InvalidArgument, "tensor storage must not be null");
        }
        return createFromRawView(tensor->dataType(), tensor->shape(), tensor->rawView());
    }

    Ort::Value &OnnxTensor::ortValue() {
        return m_value;
    }

    const Ort::Value &OnnxTensor::ortValue() const {
        return m_value;
    }

    std::string OnnxTensor::backend() const {
        return Backend;
    }

    ITensor::DataType OnnxTensor::dataType() const {
        return m_dataType;
    }
    std::vector<int64_t> OnnxTensor::shape() const {
        return m_shape;
    }
    size_t OnnxTensor::byteSize() const {
        return m_byteSize;
    }

    size_t OnnxTensor::elementCount() const {
        if (m_elementSize == 0) {
            return 0;
        }
        return m_byteSize / m_elementSize;
    }

    size_t OnnxTensor::elementSize() const {
        return m_elementSize;
    }

    const std::byte *OnnxTensor::rawData() const {
        if (!m_value || !m_value.IsTensor()) {
            return nullptr;
        }
        return static_cast<const std::byte *>(m_value.GetTensorRawData());
    }

    std::byte *OnnxTensor::mutableRawData() {
        if (!m_value || !m_value.IsTensor()) {
            return nullptr;
        }
        return static_cast<std::byte *>(m_value.GetTensorMutableRawData());
    }

    stdc::array_view<std::byte> OnnxTensor::rawView() const {
        if (!m_value || !m_value.IsTensor()) {
            return {};
        }
        return {rawData(), byteSize()};
    }

    std::shared_ptr<ITensor> OnnxTensor::clone() const {
        return createFromRawView(dataType(), shape(), rawView()).valueOr(nullptr);
    }

    bool OnnxTensor::isValid() const {
        return m_value && m_value.IsTensor() && m_dataType != Undefined;
    }

}
