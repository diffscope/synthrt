#ifndef DSINFER_TENSOR_H
#define DSINFER_TENSOR_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <stdcorelib/adt/aligned_allocator.h>
#include <stdcorelib/adt/array_view.h>

#include <synthrt/Support/Expected.h>

#include <dsinfer/dsinfer_global.h>

namespace ds {

    /// Provides type independent access to tensor storage.
    class ITensor {
    public:
        /// Identifies the element representation stored by a tensor.
        enum DataType {
            Undefined = 0,
            Float = 1,
            Bool = 2,
            Int64 = 3,
        };

        virtual ~ITensor() = default;

        /// Returns the tensor backend identifier.
        virtual std::string backend() const = 0;

        /// Returns the element data type.
        virtual DataType dataType() const = 0;

        /// Returns the tensor dimensions.
        virtual std::vector<int64_t> shape() const = 0;

        /// Returns the total storage size in bytes.
        virtual size_t byteSize() const = 0;

        /// Returns the number of elements.
        virtual size_t elementCount() const = 0;

        /// Returns the size of one element in bytes.
        virtual size_t elementSize() const = 0;

        /// Returns the immutable storage address.
        virtual const std::byte *rawData() const = 0;

        /// Returns the mutable storage address.
        virtual std::byte *mutableRawData() = 0;

        /// Returns an immutable view of the storage bytes.
        virtual stdc::array_view<std::byte> rawView() const = 0;

        /// Returns typed element data when \a T matches dataType().
        template <typename T>
        const T *constData() const;

        /// Returns mutable typed element data when \a T matches dataType().
        template <typename T>
        T *data();

        /// Returns a typed view when \a T matches dataType().
        template <typename T>
        stdc::array_view<T> view() const;

        /// Creates an independently owned deep copy.
        virtual std::shared_ptr<ITensor> clone() const = 0;
    };

    /// Maps a C++ type to an ITensor data type.
    template <typename T>
    struct TensorTraits {
        static constexpr bool isValid = false;
        static constexpr ITensor::DataType dataType = ITensor::Undefined;
    };

    template <>
    struct TensorTraits<float> {
        static constexpr bool isValid = true;
        static constexpr ITensor::DataType dataType = ITensor::Float;
    };

    template <>
    struct TensorTraits<int64_t> {
        static constexpr bool isValid = true;
        static constexpr ITensor::DataType dataType = ITensor::Int64;
    };

    template <>
    struct TensorTraits<bool> {
        static constexpr bool isValid = true;
        static constexpr ITensor::DataType dataType = ITensor::Bool;
    };

    template <typename T>
    const T *ITensor::constData() const {
        static_assert(TensorTraits<T>::isValid, "Unsupported tensor data type");
        static_assert(!std::is_same_v<T, bool> || sizeof(bool) == 1,
                      "sizeof(bool) == 1 does not satisfy");
        if (TensorTraits<T>::dataType != dataType()) {
            return nullptr;
        }
        return reinterpret_cast<const T *>(rawData());
    }

    template <typename T>
    T *ITensor::data() {
        static_assert(TensorTraits<T>::isValid, "Unsupported tensor data type");
        static_assert(!std::is_same_v<T, bool> || sizeof(bool) == 1,
                      "sizeof(bool) == 1 does not satisfy");
        if (TensorTraits<T>::dataType != dataType()) {
            return nullptr;
        }
        return reinterpret_cast<T *>(mutableRawData());
    }

    template <typename T>
    stdc::array_view<T> ITensor::view() const {
        static_assert(TensorTraits<T>::isValid, "Unsupported tensor data type");
        static_assert(!std::is_same_v<T, bool> || sizeof(bool) == 1,
                      "sizeof(bool) == 1 does not satisfy");
        if (TensorTraits<T>::dataType != dataType()) {
            return {};
        }
        return stdc::array_view<T>(reinterpret_cast<const T *>(rawData()), elementCount());
    }

    /// Stores tensor data in aligned CPU memory.
    class DSINFER_EXPORT Tensor : public ITensor {
    public:
        static constexpr size_t Alignment = sizeof(int64_t);
        static constexpr const char *Backend = "tensor";

        template <typename T>
        using AlignedVector = std::vector<T, stdc::aligned_allocator<T, Alignment>>;

        using Container = AlignedVector<std::byte>;

        Tensor() = default;
        ~Tensor() = default;

        Tensor(Tensor &&other) noexcept;
        Tensor &operator=(Tensor &&other) noexcept;

        Tensor(const Tensor &) = delete;
        Tensor &operator=(const Tensor &) = delete;

        /// Allocates a zero initialized tensor.
        static srt::Expected<std::shared_ptr<Tensor>> create(DataType dataType,
                                                             const std::vector<int64_t> &shape);

        /// Copies raw storage into a tensor.
        static srt::Expected<std::shared_ptr<Tensor>>
            createFromRawData(DataType dataType, const std::vector<int64_t> &shape,
                              const Container &data);

        /// Copies a raw storage view into a tensor.
        static srt::Expected<std::shared_ptr<Tensor>>
            createFromRawView(DataType dataType, const std::vector<int64_t> &shape,
                              const stdc::array_view<std::byte> &data);

        /// Moves raw storage into a tensor.
        static srt::Expected<std::shared_ptr<Tensor>>
            createFromRawData(DataType dataType, const std::vector<int64_t> &shape,
                              Container &&data);

        /// Copies typed element data into a tensor.
        template <typename T>
        static srt::Expected<std::shared_ptr<Tensor>>
            createFromView(const std::vector<int64_t> &shape, const stdc::array_view<T> &data);

        /// Creates a tensor containing one element.
        template <typename T>
        static srt::Expected<std::shared_ptr<Tensor>> createScalar(T value,
                                                                   bool zeroDimensions = false);

        /// Creates a tensor whose elements all equal \a value.
        template <typename T>
        static srt::Expected<std::shared_ptr<Tensor>>
            createFilled(const std::vector<int64_t> &shape, T value);

        std::string backend() const override;
        DataType dataType() const override;
        std::vector<int64_t> shape() const override;
        size_t byteSize() const override;
        size_t elementCount() const override;
        size_t elementSize() const override;
        const std::byte *rawData() const override;
        std::byte *mutableRawData() override;
        stdc::array_view<std::byte> rawView() const override;
        std::shared_ptr<ITensor> clone() const override;

    protected:
        DataType m_dataType = Undefined;
        std::vector<int64_t> m_shape;
        Container m_data;
    };

    template <typename T>
    srt::Expected<std::shared_ptr<Tensor>> Tensor::createFromView(const std::vector<int64_t> &shape,
                                                                  const stdc::array_view<T> &data) {
        static_assert(TensorTraits<T>::isValid, "Unsupported tensor data type");
        static_assert(!std::is_same_v<T, bool> || sizeof(bool) == 1,
                      "sizeof(bool) == 1 does not satisfy");

        const stdc::array_view<std::byte> rawView(reinterpret_cast<const std::byte *>(data.data()),
                                                  data.size() * sizeof(T));
        return createFromRawView(TensorTraits<T>::dataType, shape, rawView);
    }

    template <typename T>
    srt::Expected<std::shared_ptr<Tensor>> Tensor::createScalar(T value, bool zeroDimensions) {
        static_assert(TensorTraits<T>::isValid, "Unsupported tensor data type");
        static_assert(!std::is_same_v<T, bool> || sizeof(bool) == 1,
                      "sizeof(bool) == 1 does not satisfy");

        Container data(sizeof(T));
        *reinterpret_cast<T *>(data.data()) = value;
        return createFromRawData(TensorTraits<T>::dataType,
                                 zeroDimensions ? std::vector<int64_t>{} : std::vector<int64_t>{1},
                                 std::move(data));
    }

    template <typename T>
    srt::Expected<std::shared_ptr<Tensor>> Tensor::createFilled(const std::vector<int64_t> &shape,
                                                                T value) {
        static_assert(TensorTraits<T>::isValid, "Unsupported tensor data type");
        static_assert(!std::is_same_v<T, bool> || sizeof(bool) == 1,
                      "sizeof(bool) == 1 does not satisfy");

        auto result = create(TensorTraits<T>::dataType, shape);
        if (!result) {
            return result.takeError();
        }
        auto tensor = result.take();
        auto data = tensor->template data<T>();
        std::fill(data, data + tensor->elementCount(), value);
        return tensor;
    }

}

#endif // DSINFER_TENSOR_H
