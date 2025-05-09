#include "Tensor.h"

namespace ds {

    std::string Tensor::backend() const {
        return "tensor";
    }

    AbstractTensor::DataType Tensor::dataType() const {
        return _dataType;
    }

    std::vector<int> Tensor::shape() const {
        return _shape;
    }

    size_t Tensor::size() const {
        return _data.size();
    }

    std::vector<uint8_t> Tensor::data() const {
        return _data;
    }

    srt::NO<AbstractTensor> Tensor::clone() const {
        return std::make_shared<Tensor>(_dataType, _shape, _data);
    }

}