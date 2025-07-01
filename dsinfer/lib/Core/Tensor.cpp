#include "Tensor.h"

#include <cstdint>
#include <limits>

namespace ds {

    static inline size_t getElementSize(ITensor::DataType dataType) {
        switch (dataType) {
            case ITensor::Float:
                return sizeof(float);
            case ITensor::Int64:
                return sizeof(int64_t);
            case ITensor::Bool:
                return sizeof(bool);
            default:
                assert(false && "Unsupported data type");
                return 0;
        }
    }

    static inline std::optional<uint64_t> getElementCountFromShape(ITensor::DataType dataType, const std::vector<int64_t> &shape) {
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

    static inline bool verifyShape(ITensor::DataType dataType, const std::vector<int64_t> &shape, size_t dataSize) {
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

    srt::Expected<void> verify(ITensor::DataType dataType, const std::vector<int64_t> &shape, size_t dataSize) {
        if (dataType == ITensor::Undefined) {
            return srt::Error(srt::Error::InvalidArgument, "data type can not be Undefined");
        }
        if (!verifyShape(dataType, shape, dataSize)) {
            return srt::Error(srt::Error::InvalidArgument, "data size and shape mismatch");
        }
        return srt::Expected<void>();
    }

    srt::Expected<srt::NO<Tensor>> Tensor::create(DataType dataType, const std::vector<int64_t> &shape) {
        auto maybeTotalElements = getElementCountFromShape(dataType, shape);
        if (!maybeTotalElements.has_value()) {
            return srt::Error(srt::Error::InvalidArgument, "invalid shape");
        }
        const uint64_t totalElements = maybeTotalElements.value();

        const size_t elementSize = getElementSize(dataType);
        if (elementSize == 0) {
            return srt::Error(srt::Error::InvalidArgument, "invalid data type");
        }
        auto tensor = srt::NO<Tensor>::create();
        tensor->_dataType = dataType;
        tensor->_shape = shape;
        tensor->_data = Container(totalElements * elementSize, std::byte{0});
        return tensor;
    }

    srt::Expected<srt::NO<Tensor>> Tensor::createFromRawData(DataType dataType, const std::vector<int64_t> &shape, const Container &data) {
        auto tensor = srt::NO<Tensor>::create();
        if (auto exp = verify(dataType, shape, data.size()); !exp) {
            return exp.takeError();
        }
        tensor->_dataType = dataType;
        tensor->_shape = shape;
        tensor->_data = data;
        return tensor;
    }

    srt::Expected<srt::NO<Tensor>> Tensor::createFromRawView(DataType dataType, const std::vector<int64_t> &shape,
                              const stdc::array_view<std::byte> &data) {
        auto tensor = srt::NO<Tensor>::create();
        if (auto exp = verify(dataType, shape, data.size()); !exp) {
            return exp.takeError();
        }
        tensor->_dataType = dataType;
        tensor->_shape = shape;
        tensor->_data = Container{data.begin(), data.end()};
        return tensor;
    }

    srt::Expected<srt::NO<Tensor>> Tensor::createFromRawData(DataType dataType, const std::vector<int64_t> &shape, Container &&data) {
        auto tensor = srt::NO<Tensor>::create();
        if (auto exp = verify(dataType, shape, data.size()); !exp) {
            return exp.takeError();
        }
        tensor->_dataType = dataType;
        tensor->_shape = shape;
        tensor->_data = std::move(data);
        return tensor;
    }

    std::string Tensor::backend() const {
        return BACKEND;
    }

    ITensor::DataType Tensor::dataType() const {
        return _dataType;
    }

    std::vector<int64_t> Tensor::shape() const {
        return _shape;
    }

    size_t Tensor::byteSize() const {
        return _data.size();
    }

    size_t Tensor::elementCount() const {
        if (auto size = elementSize(); size > 0) {
            return byteSize() / size;
        }
        return 0;
    }

    size_t Tensor::elementSize() const {
        return getElementSize(_dataType);
    }

    const std::byte *Tensor::rawData() const {
        return _data.data();
    }

    std::byte *Tensor::mutableRawData() {
        return _data.data();
    }

    stdc::array_view<std::byte> Tensor::rawView() const {
        return {_data.data(), _data.size()};
    }

    srt::NO<ITensor> Tensor::clone() const {
        auto tensor = srt::NO<Tensor>::create();
        tensor->_dataType = _dataType;
        tensor->_shape = _shape;
        tensor->_data = _data;
        return tensor;
    }
}