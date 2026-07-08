#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <vector>

#include <stdcorelib/adt/array_view.h>

#include <synthrt/Core/Core/NamedObject.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Core/Support/AlignedAllocator.h>
#include <synthrt/Core/Tensor/ITensor.h>
#include <synthrt/Core/srt_core_global.h>

namespace srt::core {

    /// Tensor - CPU-based tensor implementation.
    ///
    /// This implementation uses a \c std::vector<std::byte> to store the tensor data,
    /// with alignment requirements to ensure proper memory access.
    class SRT_CORE_EXPORT Tensor : public ITensor {
    public:
        static constexpr size_t ALIGNMENT = sizeof(int64_t);

        template <typename T>
        using AlignedVector = std::vector<T, AlignedAllocator<T, ALIGNMENT>>;

        using Container = AlignedVector<std::byte>;

        /// Tensor backend identifier.
        static constexpr const char *BACKEND = "tensor";

        /// Default constructor, creates an empty/invalid Tensor.
        inline Tensor() : ITensor(), _dataType(Undefined), _shape{}, _data{} {
        }

        ~Tensor() override = default;

        Tensor(Tensor &&other) noexcept;
        Tensor &operator=(Tensor &&other) noexcept;

        Tensor(const Tensor &) = delete;
        Tensor &operator=(const Tensor &) = delete;

        /// \brief Allocates a new tensor with the specified data type and shape.
        ///
        /// \param dataType The data type of each element in the tensor.
        /// \param shape A vector representing the shape (dimensions) of the tensor.
        ///
        /// \return On success: A new Tensor wrapped in `NO`.
        ///         On failure: An error describing the cause of the failure.
        ///
        /// \note The returned tensor is zero-initialized.
        static Expected<NO<Tensor>> create(DataType dataType,
                                           const std::vector<int64_t> &shape);

        /// \brief Create a tensor from raw byte data.
        ///
        /// The tensor makes an internal copy of the provided data.
        ///
        /// \param dataType Element data type.
        /// \param shape Shape (dimensions) of the tensor.
        /// \param data Raw byte buffer containing the tensor data.
        ///             Must match the size implied by shape and dataType.
        ///
        /// \return On success: A new Tensor wrapped in `NO`.
        ///         On failure: An error describing the cause of the failure.
        ///
        /// \pre `shape` must represent a valid tensor shape (non-negative dimensions).
        /// \pre `data.size()` must equal the number of bytes required by `shape` and `dataType`.
        /// \post On success, the returned tensor owns its own copy of the data.
        ///       The original data is not modified.
        static Expected<NO<Tensor>> createFromRawData(DataType dataType,
                                                      const std::vector<int64_t> &shape,
                                                      const Container &data);

        /// \brief Create a tensor from a raw byte view.
        ///
        /// The tensor makes an internal copy of the provided data.
        ///
        /// \param dataType Element data type.
        /// \param shape Shape (dimensions) of the tensor.
        /// \param data View into an external byte buffer containing tensor data.
        ///             Must match the size implied by shape and dataType.
        ///
        /// \return On success: A new Tensor wrapped in `NO`.
        ///         On failure: An error describing the cause of the failure.
        ///
        /// \pre `shape` must represent a valid tensor shape (non-negative dimensions).
        /// \pre `data.size()` must equal the number of bytes required by `shape` and `dataType`.
        /// \post On success, the returned tensor owns its own copy of the data.
        ///       The original data is not modified.
        static Expected<NO<Tensor>>
            createFromRawView(DataType dataType, const std::vector<int64_t> &shape,
                              const stdc::array_view<std::byte> &data);

        /// \brief Create a tensor from an rvalue reference to raw byte data.
        ///
        /// The tensor takes ownership of the provided data via move semantics.
        ///
        /// \param dataType Element data type.
        /// \param shape Shape (dimensions) of the tensor.
        /// \param data Raw byte buffer to be moved into the tensor.
        ///
        /// \return On success: A new Tensor wrapped in `NO`.
        ///         On failure: An error describing the cause of the failure.
        ///
        /// \pre `dataType` must be a valid DataType.
        /// \pre `shape` must represent a valid tensor shape (non-negative dimensions).
        /// \pre `data.size()` must equal the number of bytes required by `shape` and `dataType`.
        /// \post On success, the returned tensor owns the data;
        ///       `data` may be left in a valid but unspecified state.
        static Expected<NO<Tensor>> createFromRawData(DataType dataType,
                                                      const std::vector<int64_t> &shape,
                                                      Container &&data);
        /// \brief Create a tensor from a typed array view.
        ///
        /// \tparam T Data type of the elements in the input view.
        ///
        /// \param shape Shape (dimensions) of the tensor.
        /// \param data View into the input data.
        ///
        /// \return On success: A new Tensor wrapped in a NamedObject.
        ///         On failure: An error describing the cause of the failure.
        ///
        /// \pre `shape` must represent a valid tensor shape (non-negative dimensions).
        /// \pre `data.size()` must match the total number of elements implied by `shape`.
        /// \post On success, the tensor contains a copy of the data as interpreted by `dataType`.
        ///       The original data is not modified.
        template <typename T>
        static Expected<NO<Tensor>> createFromView(const std::vector<int64_t> &shape,
                                                   const stdc::array_view<T> &data);

        /// \brief Create a tensor with a single value, optionally as a zero-dimension tensor
        /// (scalar).
        ///
        /// \tparam T Data type of the scalar value.
        ///
        /// \param value Scalar value to initialize the tensor with.
        /// \param zeroDimensions If true, creates a zero-dimension tensor (scalar).
        ///                       Otherwise, creates a 1D tensor of length 1.
        ///
        /// \return On success: A new Tensor wrapped in a NamedObject.
        ///         On failure: An error describing the cause of the failure.
        ///
        /// \post On success, the tensor contains one element initialized to `value`.
        template <typename T>
        static Expected<NO<Tensor>> createScalar(T value, bool zeroDimensions = false);

        /// \brief Create a tensor filled with same value.
        ///
        /// \tparam T Data type of the value.
        ///
        /// \param shape Shape (dimensions) of the tensor.
        /// \param value Scalar value to initialize the tensor with.
        ///
        /// \return On success: A new Tensor wrapped in a NamedObject.
        ///         On failure: An error describing the cause of the failure.
        template <typename T>
        static Expected<NO<Tensor>> createFilled(const std::vector<int64_t> &shape, T value);

        /// \copydoc ITensor::backend
        std::string backend() const override;

        /// \copydoc ITensor::dataType
        DataType dataType() const override;

        /// \copydoc ITensor::shape
        std::vector<int64_t> shape() const override;

        /// \copydoc ITensor::strides
        std::vector<int64_t> strides() const override;

        /// \copydoc ITensor::byteSize
        size_t byteSize() const override;

        /// \copydoc ITensor::elementCount
        size_t elementCount() const override;

        /// \copydoc ITensor::elementSize
        size_t elementSize() const override;

        /// \copydoc ITensor::rawData
        const std::byte *rawData() const override;

        /// \copydoc ITensor::mutableRawData
        std::byte *mutableRawData() override;

        /// \copydoc ITensor::rawView
        stdc::array_view<std::byte> rawView() const override;

        /// \copydoc ITensor::clone
        /// \post After cloning, the underlying backend remains the same.
        NO<ITensor> clone() const override;

        /// \copydoc ITensor::reshape
        Expected<void> reshape(const std::vector<int64_t> &shape) override;

    protected:
        DataType _dataType;
        std::vector<int64_t> _shape;
        Container _data;
    };

    inline Tensor::Tensor(Tensor &&other) noexcept
        : ITensor(), _dataType(other._dataType), _shape(std::move(other._shape)),
          _data(std::move(other._data)) {
        other._dataType = Undefined;
    }

    inline Tensor &Tensor::operator=(Tensor &&other) noexcept {
        if (this != &other) {
            _dataType = other._dataType;
            _shape = std::move(other._shape);
            _data = std::move(other._data);

            other._dataType = Undefined;
        }
        return *this;
    }

    ////////////////////////////////////////////////////////////////////////////////
    // Inline template implementations
    ////////////////////////////////////////////////////////////////////////////////
    template <typename T>
    inline Expected<NO<Tensor>> Tensor::createFromView(const std::vector<int64_t> &shape,
                                                       const stdc::array_view<T> &data) {
        static_assert(tensor_traits<T>::is_valid, "Unsupported tensor data type");
        static_assert(!std::is_same_v<T, bool> || sizeof(bool) == 1,
                      "sizeof(bool) == 1 does not satisfy");

        stdc::array_view<std::byte> rawView{reinterpret_cast<const std::byte *>(data.data()),
                                            data.size() * sizeof(T)};
        return createFromRawView(tensor_traits<T>::data_type, shape, rawView);
    }

    template <typename T>
    inline Expected<NO<Tensor>> Tensor::createScalar(T value, bool zeroDimensions) {
        static_assert(tensor_traits<T>::is_valid, "Unsupported tensor data type");
        static_assert(!std::is_same_v<T, bool> || sizeof(bool) == 1,
                      "sizeof(bool) == 1 does not satisfy");

        Container data(sizeof(T));
        *reinterpret_cast<T *>(data.data()) = value;

        return createFromRawData(tensor_traits<T>::data_type,
                                 zeroDimensions ? std::vector<int64_t>{} : std::vector<int64_t>{1},
                                 std::move(data));
    }

    template <typename T>
    inline Expected<NO<Tensor>> Tensor::createFilled(const std::vector<int64_t> &shape, T value) {
        static_assert(tensor_traits<T>::is_valid, "Unsupported tensor data type");
        static_assert(!std::is_same_v<T, bool> || sizeof(bool) == 1,
                      "sizeof(bool) == 1 does not satisfy");

        auto exp = create(tensor_traits<T>::data_type, shape);
        if (!exp) {
            return exp.takeError();
        }
        auto tensor = exp.take();
        auto dataPtr = tensor->template mutableData<T>();
        std::fill(dataPtr, dataPtr + tensor->elementCount(), value);
        return tensor;
    }

}
