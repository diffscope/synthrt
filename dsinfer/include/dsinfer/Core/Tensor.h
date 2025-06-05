#ifndef DSINFER_TENSOR_H
#define DSINFER_TENSOR_H

#include <string>
#include <vector>

#include <stdcorelib/adt/array_view.h>

#include <synthrt/Core/NamedObject.h>
#include <synthrt/Support/JSON.h>
#include <synthrt/Support/Expected.h>

#include <dsinfer/dsinfer_global.h>

namespace ds {

    class ITensor : public srt::NamedObject {
    public:
        enum DataType {
            Undefined = 0,
            Float = 1,
            Bool = 2,
            Int64 = 3,
        };

        virtual ~ITensor() = default;

        /// Tensor backend identifier.
        virtual std::string backend() const = 0;

        virtual DataType dataType() const = 0;
        virtual std::vector<int64_t> shape() const = 0;

        virtual size_t size() const = 0;
        virtual std::vector<uint8_t> data() const = 0;

        virtual srt::NO<ITensor> clone() const = 0;
    };

    class DSINFER_EXPORT Tensor : public ITensor {
    public:
        inline Tensor() : _dataType(Undefined) {
        }
        inline Tensor(DataType dataType, const std::vector<int64_t> &shape,
                      const std::vector<uint8_t> &data)
            : _dataType(dataType), _shape(shape), _data(data) {
        }
        inline Tensor(DataType dataType, std::vector<int64_t> &&shape, std::vector<uint8_t> &&data)
            : _dataType(dataType), _shape(std::move(shape)), _data(std::move(data)) {
        }

        ~Tensor() override = default;

        std::string backend() const override;

        DataType dataType() const override;
        std::vector<int64_t> shape() const override;

        size_t size() const override;
        std::vector<uint8_t> data() const override;
        srt::NO<ITensor> clone() const override;

        /// Setters
        inline void setDataType(DataType dataType) {
            _dataType = dataType;
        }
        inline void setShape(stdc::array_view<int64_t> shape) {
            _shape = shape.vec();
        }
        inline void setShape(std::vector<int64_t> &&shape) {
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
        std::vector<int64_t> _shape;
        std::vector<uint8_t> _data;
    };

}

#endif // DSINFER_TENSOR_H