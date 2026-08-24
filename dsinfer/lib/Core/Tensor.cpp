#include "Tensor.h"

#include <cstdint>
#include <limits>
#include <optional>

namespace ds {

    /// \return the size of one element, or zero for a type that has none.
    ///
    /// \note Zero rather than an assertion. \c Undefined is the state a default constructed tensor
    ///       is in, and \c create takes a data type from its caller, so reaching here with one is
    ///       ordinary rather than a mistake to trap on.
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

    static inline bool verifyShape(ITensor::DataType dataType, const std::vector<int64_t> &shape,
                                   size_t dataSize) {
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

    srt::Expected<void> verify(ITensor::DataType dataType, const std::vector<int64_t> &shape,
                               size_t dataSize) {
        if (dataType == ITensor::Undefined) {
            return srt::Error(srt::Error::InvalidArgument, "data type can not be Undefined");
        }
        if (!verifyShape(dataType, shape, dataSize)) {
            return srt::Error(srt::Error::InvalidArgument, "data size and shape mismatch");
        }
        return srt::Expected<void>();
    }

    Tensor::Tensor(Tensor &&other) noexcept
        : m_dataType(other.m_dataType), m_shape(std::move(other.m_shape)),
          m_data(std::move(other.m_data)) {
        other.m_dataType = Undefined;
    }

    Tensor &Tensor::operator=(Tensor &&other) noexcept {
        if (this != &other) {
            m_dataType = other.m_dataType;
            m_shape = std::move(other.m_shape);
            m_data = std::move(other.m_data);
            other.m_dataType = Undefined;
        }
        return *this;
    }

    srt::Expected<std::shared_ptr<Tensor>> Tensor::create(DataType dataType,
                                                          const std::vector<int64_t> &shape) {
        if (dataType == Undefined) {
            return srt::Error(srt::Error::InvalidArgument, "data type can not be Undefined");
        }
        auto maybeTotalElements = getElementCountFromShape(dataType, shape);
        if (!maybeTotalElements.has_value()) {
            return srt::Error(srt::Error::InvalidArgument, "invalid shape");
        }
        const uint64_t totalElements = maybeTotalElements.value();

        const size_t elementSize = getElementSize(dataType);
        if (elementSize == 0) {
            return srt::Error(srt::Error::InvalidArgument, "invalid data type");
        }
        auto tensor = std::make_shared<Tensor>();
        tensor->m_dataType = dataType;
        tensor->m_shape = shape;
        tensor->m_data = Container(totalElements * elementSize, std::byte{0});
        return tensor;
    }

    srt::Expected<std::shared_ptr<Tensor>>
        Tensor::createFromRawData(DataType dataType, const std::vector<int64_t> &shape,
                                  const Container &data) {
        auto tensor = std::make_shared<Tensor>();
        if (auto exp = verify(dataType, shape, data.size()); !exp) {
            return exp.takeError();
        }
        tensor->m_dataType = dataType;
        tensor->m_shape = shape;
        tensor->m_data = data;
        return tensor;
    }

    srt::Expected<std::shared_ptr<Tensor>>
        Tensor::createFromRawView(DataType dataType, const std::vector<int64_t> &shape,
                                  const stdc::array_view<std::byte> &data) {
        auto tensor = std::make_shared<Tensor>();
        if (auto exp = verify(dataType, shape, data.size()); !exp) {
            return exp.takeError();
        }
        tensor->m_dataType = dataType;
        tensor->m_shape = shape;
        tensor->m_data = Container{data.begin(), data.end()};
        return tensor;
    }

    srt::Expected<std::shared_ptr<Tensor>>
        Tensor::createFromRawData(DataType dataType, const std::vector<int64_t> &shape,
                                  Container &&data) {
        auto tensor = std::make_shared<Tensor>();
        if (auto exp = verify(dataType, shape, data.size()); !exp) {
            return exp.takeError();
        }
        tensor->m_dataType = dataType;
        tensor->m_shape = shape;
        tensor->m_data = std::move(data);
        return tensor;
    }

    std::string Tensor::backend() const {
        return Backend;
    }

    ITensor::DataType Tensor::dataType() const {
        return m_dataType;
    }

    std::vector<int64_t> Tensor::shape() const {
        return m_shape;
    }

    size_t Tensor::byteSize() const {
        return m_data.size();
    }

    size_t Tensor::elementCount() const {
        if (auto size = elementSize(); size > 0) {
            return byteSize() / size;
        }
        return 0;
    }

    size_t Tensor::elementSize() const {
        return getElementSize(m_dataType);
    }

    const std::byte *Tensor::rawData() const {
        return m_data.data();
    }

    std::byte *Tensor::mutableRawData() {
        return m_data.data();
    }

    stdc::array_view<std::byte> Tensor::rawView() const {
        return {m_data.data(), m_data.size()};
    }

    std::shared_ptr<ITensor> Tensor::clone() const {
        auto tensor = std::make_shared<Tensor>();
        tensor->m_dataType = m_dataType;
        tensor->m_shape = m_shape;
        tensor->m_data = m_data;
        return tensor;
    }

}
