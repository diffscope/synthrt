#ifndef DSINFER_ONNXTENSOR_H
#define DSINFER_ONNXTENSOR_H

#include <dsinfer/Core/Tensor.h>

#include <onnxruntime_cxx_api.h>

namespace ds {

    /**
     * @class OnnxTensor
     * @brief A wrapper around Ort::Value representing an ONNX Runtime tensor.
     *
     * This class provides an implementation of the ITensor interface and acts
     * as an adapter between the dsinfer framework and ONNX Runtime.
     */
    class OnnxTensor : public ITensor {
    public:
        static constexpr const char *BACKEND = "onnx";

        /**
         * @brief Constructs an empty (invalid) OnnxTensor.
         */
        OnnxTensor();

        OnnxTensor(OnnxTensor &&other) noexcept;

        /**
         * @brief Copy constructor is deleted.
         *
         * OnnxTensor is move-only. Use clone() to create a deep copy instead.
         */
        OnnxTensor(const OnnxTensor &) = delete;

        /**
         * @brief Copy assignment operator is deleted.
         *
         * OnnxTensor is move-only. Use clone() to create a deep copy instead.
         */
        OnnxTensor &operator=(const OnnxTensor &) = delete;

        /**
         * @brief Move assignment from another OnnxTensor
         *
         * @param other The OnnxTensor to move from.
         */
        OnnxTensor &operator=(OnnxTensor &&other) noexcept;

        /**
         * @brief Destructor of OnnxTensor.
         */
        ~OnnxTensor() override;

        static srt::Expected<srt::NO<OnnxTensor>> create(DataType, const std::vector<int64_t> &shape);

        /**
         * @brief Creates a shared OnnxTensor from data type, shape, and raw bytes.
         *
         * @param dataType The type of the tensor elements.
         * @param shape The shape of the tensor.
         * @param data The raw tensor data.
         */
        static srt::Expected<srt::NO<OnnxTensor>> createFromRawView(DataType dataType,
                                                         const std::vector<int64_t> &shape,
                                                         const stdc::array_view<std::byte> &data);

        template <typename T>
        static srt::Expected<srt::NO<OnnxTensor>> createFromView(const std::vector<int64_t> &shape,
            const stdc::array_view<T> &data);

        template <typename T>
        static srt::Expected<srt::NO<OnnxTensor>> createFromSingleValue(T value, bool zeroDimensions = false);
        /**
         * @brief Constructs an OnnxTensor by taking ownership of an existing Ort::Value.
         *
         * The caller must not use the original Ort::Value after passing it in.
         *
         * @param value The Ort::Value to take ownership of.
         */
        static srt::Expected<srt::NO<OnnxTensor>> createFromOrtValue(Ort::Value &&value);

        static srt::Expected<srt::NO<OnnxTensor>> createFromTensor(const srt::NO<ITensor> &tensor);

        /**
         * @brief Takes ownership of an existing Ort::Value. After that, the Ort::Value
         * previously held will be released and returned.
         *
         * The caller must not use the original Ort::Value after passing it in.
         *
         * @param value The Ort::Value to take ownership of.
         * @return Ort::Value previously held by this OnnxTensor object.
         */
        Ort::Value takeOrtValue(Ort::Value&& value);

        /**
         * @brief Releases ownership of the internal Ort::Value.
         *
         * After this call, the OnnxTensor becomes invalid.
         *
         * @return The Ort::Value previously held by this OnnxTensor object.
         */
        Ort::Value releaseOrtValue();

        /**
         * @brief Gets a mutable pointer to the underlying Ort::Value.
         *
         * This does not transfer ownership. The caller must not delete or move the pointer.
         *
         * @return A pointer to the internal Ort::Value.
         */
        Ort::Value *valuePtr();

        /**
         * @brief Gets a const pointer to the underlying Ort::Value.
         *
         * This does not transfer ownership. The caller must not delete or move the pointer.
         *
         * @return A const pointer to the internal Ort::Value.
         */
        const Ort::Value *valuePtr() const;

        // ITensor interface implementation
        std::string backend() const override;

        /**
         * @brief Gets the data type of the tensor.
         * @return The tensor's data type.
         */
        DataType dataType() const override;

        /**
         * @brief Gets the shape of the tensor.
         * @return A vector of dimensions.
         */
        std::vector<int64_t> shape() const override;
        size_t byteSize() const override;
        size_t elementCount() const override;
        size_t elementSize() const override;
        const std::byte *rawData() const override;
        std::byte *mutableRawData() override;
        stdc::array_view<std::byte> rawView() const override;
        srt::NO<ITensor> clone() const override;

        /**
         * @brief Checks whether the tensor is valid.
         * @return True if the internal Ort::Value is non-null and a tensor.
         */
        bool isValid() const;

    protected:
        /// The internal Ort::Value representing the tensor.
        Ort::Value _value;

        // Cached metadata
        DataType _dataType;
        std::vector<int64_t> _shape;
        size_t _elementSize;
        size_t _bytesSize;
    };

    template <typename T>
    inline srt::Expected<srt::NO<OnnxTensor>>
        OnnxTensor::createFromView(const std::vector<int64_t> &shape,
                                   const stdc::array_view<T> &data) {
        static_assert(tensor_traits<T>::is_valid, "Unsupported tensor data type");
        static_assert(!std::is_same_v<T, bool> || sizeof(bool) == 1,
                      "sizeof(bool) == 1 does not satisfy");

        stdc::array_view<std::byte> rawView{reinterpret_cast<const std::byte *>(data.data()),
                                            data.size() * sizeof(T)};
        return createFromRawView(tensor_traits<T>::data_type, shape, rawView);
    }

    template <typename T>
    inline srt::Expected<srt::NO<OnnxTensor>> OnnxTensor::createFromSingleValue(T value, bool zeroDimensions) {
        static_assert(tensor_traits<T>::is_valid, "Unsupported tensor data type");
        static_assert(!std::is_same_v<T, bool> || sizeof(bool) == 1,
                      "sizeof(bool) == 1 does not satisfy");

        Tensor::Container data(sizeof(T));
        stdc::array_view<std::byte> rawView(data.data(), data.size());

        return createFromRawView(tensor_traits<T>::data_type, zeroDimensions ? std::vector<int64_t>{} : std::vector<int64_t>{1},rawView);
    }
}

#endif // DSINFER_ONNXTENSOR_H