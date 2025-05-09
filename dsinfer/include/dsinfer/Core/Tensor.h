#ifndef DSINFER_TENSOR_H
#define DSINFER_TENSOR_H

#include <string>
#include <vector>

#include <synthrt/Core/NamedObject.h>

#include <dsinfer/dsinfer_global.h>

namespace ds {

    class AbstractTensor : public srt::NamedObject {
    public:
        enum DataType {
            Float = 1,
            Bool = 2,
            Int = 3,
            Int64 = 3,
        };

        virtual ~AbstractTensor() = default;

        /// Tensor backend identifier.
        virtual std::string backend() const = 0;

        virtual DataType dataType() const = 0;
        virtual std::vector<int> shape() const = 0;

        virtual size_t size() const = 0;
        virtual std::vector<uint8_t> data() const = 0;

        virtual AbstractTensor *clone() const = 0;
    };

    class Tensor : public AbstractTensor {
    public:
        Tensor() : _dataType(DataType::Float) {
        }
        Tensor(DataType dataType, const std::vector<int> &shape, const std::vector<uint8_t> &data)
            : _dataType(dataType), _shape(shape), _data(data) {
        }
        Tensor(DataType dataType, std::vector<int> &&shape, std::vector<uint8_t> &&data)
            : _dataType(dataType), _shape(std::move(shape)), _data(std::move(data)) {
        }

        std::string backend() const override {
            return "tensor";
        }

        DataType dataType() const override {
            return _dataType;
        }
        std::vector<int> shape() const override {
            return _shape;
        }

        size_t size() const override {
            return _data.size();
        }
        std::vector<uint8_t> data() const override {
            return _data;
        }
        AbstractTensor *clone() const override {
            return new Tensor(_dataType, _shape, _data);
        }

    private:
        DataType _dataType;
        std::vector<int> _shape;
        std::vector<uint8_t> _data;
    };

}

#endif // DSINFER_TENSOR_H