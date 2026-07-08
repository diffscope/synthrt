#ifndef DSINFER_INFERUTIL_TENSORHELPER_H
#define DSINFER_INFERUTIL_TENSORHELPER_H

#include <cstddef>
#include <cstdint>
#include <vector>
#include <synthrt/Core/srt_core_global.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Core/Tensor/ITensor.h>
#include <synthrt/Core/Tensor/Tensor.h>


namespace ds::infer::inferutil {
    template <typename T>
    class TensorHelper {
    public:
        static srt::core::Expected<TensorHelper> createFor1DArray(size_t size) {
            TensorHelper helper;
            std::vector<int64_t> shape{1, static_cast<int64_t>(size)};
            auto exp = srt::core::Tensor::create(srt::core::tensor_traits<T>::data_type, shape);
            if (!exp) {
                return exp.takeError();
            }
            helper._tensor = exp.take();
            auto dataPtr = helper._tensor->template mutableData<T>();
            if (STDCORELIB_UNLIKELY(dataPtr == nullptr)) {
                return srt::core::Error(srt::core::Error::SessionError, "failed to create tensor");
            }
            helper._current = dataPtr;
            helper._end = dataPtr + size;
            return helper;
        }

        inline bool write(T value) {
            if (_current >= _end) {
                return false;
            }
            *_current++ = value;
            return true;
        }

        inline void writeUnchecked(T value) {
            *_current++ = value;
        }

        inline bool isComplete() const {
            return _current == _end;
        }

        inline srt::core::NO<srt::core::Tensor> &value() {
            return _tensor;
        }

        inline srt::core::NO<srt::core::Tensor> &&take() {
            return std::move(_tensor);
        }

        STDCORELIB_DISABLE_COPY(TensorHelper)

        TensorHelper(TensorHelper &&other) noexcept
            : _tensor(std::move(other._tensor)), _current(other._current), _end(other._end) {
            other._current = nullptr;
            other._end = nullptr;
        }

        TensorHelper &operator=(TensorHelper &&other) noexcept {
            if (this != &other) {
                _tensor = std::move(other._tensor);
                _current = other._current;
                _end = other._end;

                other._current = nullptr;
                other._end = nullptr;
            }
            return *this;
        }

    private:
        TensorHelper() : _current(nullptr), _end(nullptr) {};

        srt::core::NO<srt::core::Tensor> _tensor;
        T *_current;
        const T *_end;
    };
}
#endif // DSINFER_INFERUTIL_TENSORHELPER_H
