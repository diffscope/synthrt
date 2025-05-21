#include "Tensor.h"

namespace ds {

    std::string Tensor::backend() const {
        return "tensor";
    }

    ITensor::DataType Tensor::dataType() const {
        return _dataType;
    }

    std::vector<int64_t> Tensor::shape() const {
        return _shape;
    }

    size_t Tensor::size() const {
        return _data.size();
    }

    std::vector<uint8_t> Tensor::data() const {
        return _data;
    }

    srt::NO<ITensor> Tensor::clone() const {
        return std::make_shared<Tensor>(_dataType, _shape, _data);
    }

}