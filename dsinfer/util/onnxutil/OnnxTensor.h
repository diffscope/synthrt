#ifndef DSINFER_ONNXTENSOR_H
#define DSINFER_ONNXTENSOR_H

#include <dsinfer/Core/Tensor.h>

#include <onnxruntime_cxx_api.h>

namespace ds {

    class OnnxTensor : public Tensor {
    public:
        explicit OnnxTensor(Ort::Value *value) : _value(value) {
        }
        ~OnnxTensor();

        std::string backend() const override {
            return "onnx";
        }

        DataType dataType() const override;
        std::vector<int64_t> shape() const override;
        size_t size() const override;
        std::vector<uint8_t> data() const override;
        srt::NO<ITensor> clone() const override;

        Ort::Value *value() const {
            return _value;
        }

    protected:
        Ort::Value *_value = nullptr;
    };

}

#endif // DSINFER_ONNXTENSOR_H