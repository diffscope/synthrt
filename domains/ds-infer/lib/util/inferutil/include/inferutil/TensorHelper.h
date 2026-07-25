#pragma once

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
            helper.m_tensor = exp.take();
            auto dataPtr = helper.m_tensor->template mutableData<T>();
            if (STDCORELIB_UNLIKELY(dataPtr == nullptr)) {
                return srt::core::Error(srt::core::Error::SessionError, "failed to create tensor");
            }
            helper.m_current = dataPtr;
            helper.m_end = dataPtr + size;
            return helper;
        }

        inline bool write(T value) {
            if (m_current >= m_end) {
                return false;
            }
            *m_current++ = value;
            return true;
        }

        inline void writeUnchecked(T value) {
            *m_current++ = value;
        }

        inline bool isComplete() const {
            return m_current == m_end;
        }

        inline srt::core::NO<srt::core::Tensor> &value() {
            return m_tensor;
        }

        inline srt::core::NO<srt::core::Tensor> &&take() {
            return std::move(m_tensor);
        }

        STDCORELIB_DISABLE_COPY(TensorHelper)

        TensorHelper(TensorHelper &&other) noexcept
            : m_tensor(std::move(other.m_tensor)), m_current(other.m_current), m_end(other.m_end) {
            other.m_current = nullptr;
            other.m_end = nullptr;
        }

        TensorHelper &operator=(TensorHelper &&other) noexcept {
            if (this != &other) {
                m_tensor = std::move(other.m_tensor);
                m_current = other.m_current;
                m_end = other.m_end;

                other.m_current = nullptr;
                other.m_end = nullptr;
            }
            return *this;
        }

    private:
        TensorHelper() : m_current(nullptr), m_end(nullptr) {};

        srt::core::NO<srt::core::Tensor> m_tensor;
        T *m_current;
        const T *m_end;
    };
}
