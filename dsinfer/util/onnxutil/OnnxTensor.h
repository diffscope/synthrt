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
        /**
         * @brief Constructs an empty (invalid) OnnxTensor.
         */
        OnnxTensor();

        /**
         * @brief Constructs an OnnxTensor from data type, shape, and raw bytes.
         *
         * @param dataType The type of the tensor elements.
         * @param shape The shape of the tensor.
         * @param data The raw tensor data.
         */
        OnnxTensor(DataType dataType, const std::vector<int64_t> &shape,
                   const std::vector<uint8_t> &data);

        /**
         * @brief Constructs an OnnxTensor from a Tensor object.
         *
         * @param tensor The source tensor to copy.
         */
        explicit OnnxTensor(const Tensor &tensor);

        /**
         * @brief Constructs an OnnxTensor by taking ownership of an existing Ort::Value.
         *
         * The caller must not use the original Ort::Value after passing it in.
         *
         * @param value The Ort::Value to take ownership of.
         */
        explicit OnnxTensor(Ort::Value &&value);

        /**
         * @brief Constructs an OnnxTensor by moving from another OnnxTensor.
         *
         * Transfers ownership of the internal Ort::Value from the source to this object.
         * After the move, the source OnnxTensor becomes invalid.
         *
         * @param other The OnnxTensor to move from.
         */
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

        /**
         * @brief Gets the backend name.
         * @return A string identifier for the backend.
         */
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

        /**
         * @brief Gets the size in bytes of the tensor data.
         * @return The total size in bytes.
         */
        size_t size() const override;

        /**
         * @brief Gets a copy of the raw tensor data.
         * @return A vector of bytes representing the tensor's contents.
         */
        std::vector<uint8_t> data() const override;

        /**
         * @brief Creates a deep copy of this tensor.
         * @return A new ITensor instance with identical contents.
         */
        srt::NO<ITensor> clone() const override;

        /**
         * @brief Checks whether the tensor is valid.
         * @return True if the internal Ort::Value is non-null and a tensor.
         */
        bool isValid() const;

    protected:
        /// The internal Ort::Value representing the tensor.
        Ort::Value _value;
    };

}

#endif // DSINFER_ONNXTENSOR_H