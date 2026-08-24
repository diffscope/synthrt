#ifndef DSINFER_ONNXTENSOR_H
#define DSINFER_ONNXTENSOR_H

#include <algorithm>

#include <dsinfer/Core/Tensor.h>
#include <dsinfer/Support/ErrorCode.h>

#include <onnxruntime_cxx_api.h>

namespace ds {

    /// Adapts an ONNX Runtime tensor to the ITensor interface.
    class OnnxTensor : public ITensor {
    public:
        static constexpr const char *BACKEND = "onnx";

        /// Constructs an invalid tensor without an underlying c Ort::Value.
        OnnxTensor();

        /// Moves the underlying c Ort::Value and cached tensor metadata.
        OnnxTensor(OnnxTensor &&other) noexcept;

        OnnxTensor(const OnnxTensor &) = delete;
        OnnxTensor &operator=(const OnnxTensor &) = delete;

        /// Replaces this tensor by moving from a other.
        OnnxTensor &operator=(OnnxTensor &&other) noexcept;

        ~OnnxTensor();

        /// Allocates an uninitialized ONNX tensor with a dataType and a shape.
        static srt::Expected<std::shared_ptr<OnnxTensor>> create(DataType dataType,
                                                                 const std::vector<int64_t> &shape);

        /// Copies a data into an ONNX tensor with a dataType and a shape.
        static srt::Expected<std::shared_ptr<OnnxTensor>>
            createFromRawView(DataType dataType, const std::vector<int64_t> &shape,
                              const stdc::array_view<std::byte> &data);

        /// Copies typed a data into an ONNX tensor with a shape.
        template <typename T>
        static srt::Expected<std::shared_ptr<OnnxTensor>>
            createFromView(const std::vector<int64_t> &shape, const stdc::array_view<T> &data);

        /// Creates an ONNX tensor containing the scalar a value.
        template <typename T>
        static srt::Expected<std::shared_ptr<OnnxTensor>> createScalar(T value,
                                                                       bool zeroDimensions = false);

        /// Creates an ONNX tensor with every element initialized to a value.
        template <typename T>
        static srt::Expected<std::shared_ptr<OnnxTensor>>
            createFilled(const std::vector<int64_t> &shape, T value);

        /// Takes ownership of a tensor c Ort::Value.
        static srt::Expected<std::shared_ptr<OnnxTensor>> createFromOrtValue(Ort::Value &&value);

        /// Copies a tensor into ONNX Runtime owned storage.
        static srt::Expected<std::shared_ptr<OnnxTensor>>
            createFromTensor(const std::shared_ptr<ITensor> &tensor);

        /// Replaces the underlying value and returns the previously owned c Ort::Value.
        Ort::Value takeOrtValue(Ort::Value &&value);

        /// Releases the underlying c Ort::Value and leaves this tensor invalid.
        Ort::Value releaseOrtValue();

        /// Returns mutable access to the owned c Ort::Value without transferring ownership.
        Ort::Value *valuePtr();

        /// Returns immutable access to the owned c Ort::Value.
        const Ort::Value *valuePtr() const;

        /// Returns c onnx.
        std::string backend() const override;

        /// Returns the cached tensor element type.
        DataType dataType() const override;

        /// Returns the cached tensor dimensions.
        std::vector<int64_t> shape() const override;

        /// Returns the tensor storage size in bytes.
        size_t byteSize() const override;

        /// Returns the number of tensor elements.
        size_t elementCount() const override;

        /// Returns the size of one tensor element in bytes.
        size_t elementSize() const override;

        /// Returns immutable access to the tensor storage.
        const std::byte *rawData() const override;

        /// Returns mutable access to the tensor storage.
        std::byte *mutableRawData() override;

        /// Returns an immutable view of the tensor storage.
        stdc::array_view<std::byte> rawView() const override;

        /// Creates an independently owned deep copy.
        std::shared_ptr<ITensor> clone() const override;

        /// Returns whether this object owns a supported tensor c Ort::Value.
        bool isValid() const;

    protected:
        /// Owns the ONNX Runtime tensor.
        Ort::Value _value;

        /// Caches the element type exposed through ITensor.
        DataType _dataType;

        /// Caches the tensor dimensions.
        std::vector<int64_t> _shape;

        /// Caches the size of one element in bytes.
        size_t _elementSize;

        /// Caches the total storage size in bytes.
        size_t _bytesSize;
    };

    template <typename T>
    inline srt::Expected<std::shared_ptr<OnnxTensor>>
        OnnxTensor::createFromView(const std::vector<int64_t> &shape,
                                   const stdc::array_view<T> &data) {
        static_assert(TensorTraits<T>::isValid, "Unsupported tensor data type");
        static_assert(!std::is_same_v<T, bool> || sizeof(bool) == 1,
                      "sizeof(bool) == 1 does not satisfy");

        stdc::array_view<std::byte> rawView{reinterpret_cast<const std::byte *>(data.data()),
                                            data.size() * sizeof(T)};
        return createFromRawView(TensorTraits<T>::dataType, shape, rawView);
    }

    template <typename T>
    inline srt::Expected<std::shared_ptr<OnnxTensor>>
        OnnxTensor::createScalar(T value, bool zeroDimensions) {
        static_assert(TensorTraits<T>::isValid, "Unsupported tensor data type");
        static_assert(!std::is_same_v<T, bool> || sizeof(bool) == 1,
                      "sizeof(bool) == 1 does not satisfy");

        Tensor::Container data(sizeof(T));
        stdc::array_view<std::byte> rawView(data.data(), data.size());

        return createFromRawView(TensorTraits<T>::dataType,
                                 zeroDimensions ? std::vector<int64_t>{} : std::vector<int64_t>{1},
                                 rawView);
    }

    template <typename T>
    inline srt::Expected<std::shared_ptr<OnnxTensor>>
        OnnxTensor::createFilled(const std::vector<int64_t> &shape, T value) {

        static_assert(TensorTraits<T>::isValid, "Unsupported tensor data type");
        static_assert(!std::is_same_v<T, bool> || sizeof(bool) == 1,
                      "sizeof(bool) == 1 does not satisfy");

        auto exp = create(TensorTraits<T>::dataType, shape);
        if (!exp) {
            return exp.takeError();
        }
        auto tensor = exp.take();
        auto dataPtr = tensor->template data<T>();
        std::fill(dataPtr, dataPtr + tensor->elementCount(), value);
        return tensor;
    }

}

#endif // DSINFER_ONNXTENSOR_H
