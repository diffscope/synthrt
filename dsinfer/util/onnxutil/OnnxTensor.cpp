#include "OnnxTensor.h"

namespace ds {

    OnnxTensor::~OnnxTensor() {
        // TODO
    }

    ITensor::DataType OnnxTensor::dataType() const {
        return {};
    }

    std::vector<int64_t> OnnxTensor::shape() const {
        return {};
    }

    size_t OnnxTensor::size() const {
        return {};
    }

    std::vector<uint8_t> OnnxTensor::data() const {
        return {};
    }

    srt::NO<ITensor> OnnxTensor::clone() const {
        return {};
    }

}