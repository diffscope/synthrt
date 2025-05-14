#ifndef DSINFER_TENSOR_H
#define DSINFER_TENSOR_H

#include <string>
#include <vector>

#include <stdcorelib/adt/array_view.h>

#include <synthrt/Core/NamedObject.h>
#include <synthrt/Support/JSON.h>
#include <synthrt/Support/Error.h>

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

        virtual srt::NO<AbstractTensor> clone() const = 0;
    };

    class DSINFER_EXPORT Tensor : public AbstractTensor {
    public:
        inline Tensor() : _dataType(Float) {
        }
        inline Tensor(DataType dataType, const std::vector<int> &shape,
                      const std::vector<uint8_t> &data)
            : _dataType(dataType), _shape(shape), _data(data) {
        }
        inline Tensor(DataType dataType, std::vector<int> &&shape, std::vector<uint8_t> &&data)
            : _dataType(dataType), _shape(std::move(shape)), _data(std::move(data)) {
        }

        std::string backend() const override;

        DataType dataType() const override;
        std::vector<int> shape() const override;

        size_t size() const override;
        std::vector<uint8_t> data() const override;
        srt::NO<AbstractTensor> clone() const override;

        /// Setters
        inline void setDataType(DataType dataType) {
            _dataType = dataType;
        }
        inline void setShape(stdc::array_view<int> shape) {
            _shape = shape.vec();
        }
        inline void setShape(std::vector<int> &&shape) {
            _shape = std::move(shape);
        }
        inline void setData(stdc::array_view<uint8_t> data) {
            _data = data.vec();
        }
        inline void setData(std::vector<uint8_t> &&data) {
            _data = std::move(data);
        }

    protected:
        DataType _dataType;
        std::vector<int> _shape;
        std::vector<uint8_t> _data;
    };

}

#endif // DSINFER_TENSOR_H