#ifndef DSINFER_TENSOR_H
#define DSINFER_TENSOR_H

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <vector>

#include <stdcorelib/adt/array_view.h>

#include <synthrt/Core/NamedObject.h>
#include <synthrt/Support/Expected.h>

#include <dsinfer/dsinfer_global.h>
#include <dsinfer/Core/AlignedAllocator.h>

namespace ds {

    class ITensor : public srt::NamedObject {
    public:
        /// Supported element data types
        enum DataType {
            Undefined = 0,
            Float = 1,
            Bool = 2,
            Int64 = 3,
            // Note: after adding new types here, please register type traits using
            // DSINFER_TENSOR_REGISTER_DATATYPE(_CppType, _EnumType) macro (see below)
        };

        virtual ~ITensor() = default;

        /// Get the tensor backend identifier.
        virtual std::string backend() const = 0;

        /// Get the element data type.
        virtual DataType dataType() const = 0;

        /// Get the shape (dimensions) of the tensor.
        virtual std::vector<int64_t> shape() const = 0;

        /// Get the total size in bytes of total elements in the tensor.
        virtual size_t byteSize() const = 0;

        /// Get the total number of elements in the tensor.
        virtual size_t elementCount() const = 0;

        /// Get the size in bytes of a single element in the tensor.
        virtual size_t elementSize() const = 0;

        /// Get a const pointer to the raw data inside the tensor.
        virtual const std::byte *rawData() const = 0;

        /// Get a mutable pointer to the raw data inside the tensor.
        virtual std::byte *mutableRawData() = 0;

        /// Get a view over the raw tensor bytes.
        virtual stdc::array_view<std::byte> rawView() const = 0;

        /// Get a typed const pointer to element data.
        /// \tparam T  C++ element type. Must match dataType().
        /// \return nullptr if T does not match dataType(), otherwise pointer to the first element.
        template <typename T>
        const T *data() const;

        /// Get a typed mutable pointer to element data.
        /// \tparam T  C++ element type. Must match dataType().
        /// \return nullptr if T does not match dataType(), otherwise pointer to the first element.
        template <typename T>
        T *mutableData();

        /// Typed view over element data.
        /// \tparam T C++ element type. Must match dataType().
        /// \return stdc::array_view<T> of length elementCount(), empty if type mismatch.
        template <typename T>
        stdc::array_view<T> view() const;

        /// Create a deep copy of this tensor as an ITensor object.
        virtual srt::NO<ITensor> clone() const = 0;
    };

    template <typename T>
    struct tensor_traits {
        /// Indicates whether the type is a supported tensor type.
        static constexpr bool is_valid = false;

        /// Corresponding ITensor::DataType enum value.
        static constexpr ITensor::DataType data_type = ITensor::DataType::Undefined;
    };


/// Macro to register a C++ type with the DataType enum.
#define DSINFER_TENSOR_REGISTER_DATATYPE(_CppType, _EnumType)                                      \
    template <>                                                                                    \
    struct tensor_traits<_CppType> {                                                               \
        static constexpr bool is_valid = true;                                                     \
        static constexpr ITensor::DataType data_type = ITensor::DataType::_EnumType;               \
    };

    // Register ITensor supported data types here. Must match ITensor::DataType enums
    DSINFER_TENSOR_REGISTER_DATATYPE(float, Float)
    DSINFER_TENSOR_REGISTER_DATATYPE(int64_t, Int64)
    DSINFER_TENSOR_REGISTER_DATATYPE(bool, Bool)

#undef DSINFER_TENSOR_REGISTER_DATATYPE

    template <typename T>
    const T *ITensor::data() const {
        static_assert(tensor_traits<T>::is_valid, "Unsupported tensor data type");
        static_assert(!std::is_same_v<T, bool> || sizeof(bool) == 1,
                      "sizeof(bool) == 1 does not satisfy");
        if (tensor_traits<T>::data_type != dataType()) {
            return nullptr;
        }
        return reinterpret_cast<const T *>(rawData());
    }

    template <typename T>
    T *ITensor::mutableData() {
        static_assert(tensor_traits<T>::is_valid, "Unsupported tensor data type");
        static_assert(!std::is_same_v<T, bool> || sizeof(bool) == 1,
                      "sizeof(bool) == 1 does not satisfy");
        if (tensor_traits<T>::data_type != dataType()) {
            return nullptr;
        }
        return reinterpret_cast<T *>(mutableRawData());
    }

    template <typename T>
    stdc::array_view<T> ITensor::view() const {
        static_assert(tensor_traits<T>::is_valid, "Unsupported tensor data type");
        static_assert(!std::is_same_v<T, bool> || sizeof(bool) == 1,
                      "sizeof(bool) == 1 does not satisfy");
        if (tensor_traits<T>::data_type != dataType()) {
            return stdc::array_view<T>();
        }
        return {reinterpret_cast<const T *>(rawData()), elementCount()};
    }


    class DSINFER_EXPORT Tensor : public ITensor {
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
        /// \return On success: A new Tensor wrapped in `srt::NO`.
        ///         On failure: An error describing the cause of the failure.
        ///
        /// \note The returned tensor is zero-initialized.
        static srt::Expected<srt::NO<Tensor>> create(DataType dataType,
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
        /// \return On success: A new Tensor wrapped in `srt::NO`.
        ///         On failure: An error describing the cause of the failure.
        ///
        /// \pre `shape` must represent a valid tensor shape (non-negative dimensions).
        /// \pre `data.size()` must equal the number of bytes required by `shape` and `dataType`.
        /// \post On success, the returned tensor owns its own copy of the data.
        ///       The original data is not modified.
        static srt::Expected<srt::NO<Tensor>> createFromRawData(DataType dataType,
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
        /// \return On success: A new Tensor wrapped in `srt::NO`.
        ///         On failure: An error describing the cause of the failure.
        ///
        /// \pre `shape` must represent a valid tensor shape (non-negative dimensions).
        /// \pre `data.size()` must equal the number of bytes required by `shape` and `dataType`.
        /// \post On success, the returned tensor owns its own copy of the data.
        ///       The original data is not modified.
        static srt::Expected<srt::NO<Tensor>>
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
        /// \return On success: A new Tensor wrapped in `srt::NO`.
        ///         On failure: An error describing the cause of the failure.
        ///
        /// \pre `dataType` must be a valid DataType.
        /// \pre `shape` must represent a valid tensor shape (non-negative dimensions).
        /// \pre `data.size()` must equal the number of bytes required by `shape` and `dataType`.
        /// \post On success, the returned tensor owns the data;
        ///       `data` may be left in a valid but unspecified state.
        static srt::Expected<srt::NO<Tensor>> createFromRawData(DataType dataType,
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
        static srt::Expected<srt::NO<Tensor>> createFromView(const std::vector<int64_t> &shape,
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
        static srt::Expected<srt::NO<Tensor>> createScalar(T value, bool zeroDimensions = false);

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
        static srt::Expected<srt::NO<Tensor>> createFilled(const std::vector<int64_t> &shape,
                                                           T value);

        /// \copydoc ITensor::backend
        std::string backend() const override;

        /// \copydoc ITensor::dataType
        DataType dataType() const override;

        /// \copydoc ITensor::shape
        std::vector<int64_t> shape() const override;

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
        srt::NO<ITensor> clone() const override;

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
    inline srt::Expected<srt::NO<Tensor>> Tensor::createFromView(const std::vector<int64_t> &shape,
                                                                 const stdc::array_view<T> &data) {
        static_assert(tensor_traits<T>::is_valid, "Unsupported tensor data type");
        static_assert(!std::is_same_v<T, bool> || sizeof(bool) == 1,
                      "sizeof(bool) == 1 does not satisfy");

        stdc::array_view<std::byte> rawView{reinterpret_cast<const std::byte *>(data.data()),
                                            data.size() * sizeof(T)};
        return createFromRawView(tensor_traits<T>::data_type, shape, rawView);
    }

    template <typename T>
    inline srt::Expected<srt::NO<Tensor>> Tensor::createScalar(T value, bool zeroDimensions) {
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
    inline srt::Expected<srt::NO<Tensor>> Tensor::createFilled(const std::vector<int64_t> &shape,
                                                               T value) {

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

#endif // DSINFER_TENSOR_H