#pragma once

#include <cstdint>
#include <vector>

#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Core/Tensor/Tensor.h>
#include <synthrt/G2P/Support/Error.h>

namespace srt::g2p::plugins::InferUtil
{
    using namespace srt::core;

    template <typename T>
    class TensorHelper {
    public:
        static Expected<TensorHelper> createFor1DArray(size_t size) {
            TensorHelper helper;
            const std::vector shape{1, static_cast<int64_t>(size)};
            auto exp = Tensor::create(tensor_traits<T>::data_type, shape);
            if (!exp) {
                return exp.takeError();
            }
            helper._tensor = exp.take();
            auto dataPtr = helper._tensor->mutableData<T>();
            if (STDCORELIB_UNLIKELY(dataPtr == nullptr)) {
                return srt::g2p::Error(srt::g2p::Error::RuntimeError, "failed to create tensor");
            }
            helper._current = dataPtr;
            helper._end = dataPtr + size;
            return helper;
        }

        bool write(T value) {
            if (_current >= _end) {
                return false;
            }
            *_current++ = value;
            return true;
        }

        void writeUnchecked(T value) { *_current++ = value; }

        bool isComplete() const { return _current == _end; }

        NO<Tensor> &value() { return _tensor; }

        NO<Tensor> &&take() { return std::move(_tensor); }

        STDCORELIB_DISABLE_COPY(TensorHelper)

        TensorHelper(TensorHelper &&other) noexcept :
            _tensor(std::move(other._tensor)), _current(other._current), _end(other._end) {
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
        TensorHelper() : _current(nullptr), _end(nullptr) {}

        NO<Tensor> _tensor;
        T *_current;
        const T *_end;
    };
} // namespace srt::g2p::plugins::InferUtil
