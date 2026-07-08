#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

#include <stdcorelib/adt/array_view.h>

#include <synthrt/Core/Core/NamedObject.h>
#include <synthrt/Core/Support/Expected.h>

namespace srt::core {

    /// Float16 - Minimal storage type for half-precision floating-point numbers.
    ///
    /// This type only provides 2-byte storage so that tensors of Float16 elements can be
    /// created and inspected via raw bytes. Arithmetic/conversion helpers are intentionally
    /// omitted; callers are responsible for any IEEE-754 half interpretation.
    struct Float16 {
        std::uint16_t value;

        Float16() noexcept = default;
        explicit Float16(std::uint16_t v) noexcept : value(v) {
        }
    };

    /// ITensor - Tensor-like object interface.
    class ITensor : public NamedObject {
    public:
        /// Supported element data types
        enum DataType {
            Undefined = 0,
            Float = 1,
            Bool = 2,
            Int64 = 3,
            Float16 = 4,
            Float64 = 5,
            Int32 = 6,
            UInt8 = 7,
            // Note: after adding new types here, please register type traits using
            // SRT_CORE_TENSOR_REGISTER_DATATYPE(_CppType, _EnumType) macro (see below)
        };

        virtual ~ITensor() = default;

        /// Get the tensor backend identifier.
        virtual std::string backend() const = 0;

        /// Get the element data type.
        virtual DataType dataType() const = 0;

        /// Get the shape (dimensions) of the tensor.
        virtual std::vector<int64_t> shape() const = 0;

        /// Get the strides (in elements) of the tensor.
        ///
        /// For a contiguous, row-major tensor, \c strides()[i] is the number of elements to
        /// skip to advance one position along dimension \c i. The returned vector has the
        /// same length as \c shape().
        virtual std::vector<int64_t> strides() const = 0;

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
        virtual NO<ITensor> clone() const = 0;

        /// \brief Reshape the tensor in place to the given shape.
        ///
        /// The total number of elements implied by the new shape must equal the current
        /// \c elementCount(). Backends that cannot perform an in-place reshape should return
        /// an \c Error of type \c NotImplemented.
        ///
        /// \param shape New shape (dimensions). An empty vector represents a 0-d (scalar) tensor.
        /// \return \c Expected<void> indicating success or the cause of failure.
        virtual Expected<void> reshape(const std::vector<int64_t> &shape) = 0;
    };

    template <typename T>
    struct tensor_traits {
        /// Indicates whether the type is a supported tensor type.
        static constexpr bool is_valid = false;

        /// Corresponding ITensor::DataType enum value.
        static constexpr ITensor::DataType data_type = ITensor::DataType::Undefined;
    };


/// Macro to register a C++ type with the DataType enum.
#define SRT_CORE_TENSOR_REGISTER_DATATYPE(_CppType, _EnumType)                                      \
    template <>                                                                                    \
    struct tensor_traits<_CppType> {                                                               \
        static constexpr bool is_valid = true;                                                     \
        static constexpr ITensor::DataType data_type = ITensor::DataType::_EnumType;               \
    };

    // Register ITensor supported data types here. Must match ITensor::DataType enums
    SRT_CORE_TENSOR_REGISTER_DATATYPE(float, Float)
    SRT_CORE_TENSOR_REGISTER_DATATYPE(int64_t, Int64)
    SRT_CORE_TENSOR_REGISTER_DATATYPE(bool, Bool)
    SRT_CORE_TENSOR_REGISTER_DATATYPE(Float16, Float16)
    SRT_CORE_TENSOR_REGISTER_DATATYPE(double, Float64)
    SRT_CORE_TENSOR_REGISTER_DATATYPE(int32_t, Int32)
    SRT_CORE_TENSOR_REGISTER_DATATYPE(uint8_t, UInt8)

#undef SRT_CORE_TENSOR_REGISTER_DATATYPE

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

}
