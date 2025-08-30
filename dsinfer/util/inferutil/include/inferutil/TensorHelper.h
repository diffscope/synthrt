#ifndef DSINFER_INFERUTIL_TENSORHELPER_H
#define DSINFER_INFERUTIL_TENSORHELPER_H

#include <cstddef>
#include <cstdint>
#include <vector>
#include <synthrt/synthrt_global.h>
#include <synthrt/Support/Expected.h>
#include <dsinfer/Core/Tensor.h>


namespace ds::inferutil {
    template <typename T>
    class TensorHelper {
    public:
        static srt::Expected<TensorHelper> createFor1DArray(size_t size) {
            TensorHelper helper;
            std::vector<int64_t> shape{1, static_cast<int64_t>(size)};
            auto exp = Tensor::create(tensor_traits<T>::data_type, shape);
            if (!exp) {
                return exp.takeError();
            }
            helper._tensor = exp.take();
            auto dataPtr = helper._tensor->template mutableData<T>();
            if (STDCORELIB_UNLIKELY(dataPtr == nullptr)) {
                return srt::Error(srt::Error::SessionError, "failed to create tensor");
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

        inline srt::NO<Tensor> &value() {
            return _tensor;
        }

        inline srt::NO<Tensor> &&take() {
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

        srt::NO<Tensor> _tensor;
        T *_current;
        const T *_end;
    };
}
#endif // DSINFER_INFERUTIL_TENSORHELPER_H
