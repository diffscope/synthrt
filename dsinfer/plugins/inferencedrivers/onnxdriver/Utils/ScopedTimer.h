#ifndef DSINFER_ONNXDRIVER_SCOPEDTIMER_H
#define DSINFER_ONNXDRIVER_SCOPEDTIMER_H

#include <chrono>
#include <functional>
#include <utility>

namespace ds::onnxdriver {

    /// Invokes a callback with the elapsed time when this scope ends.
    class ScopedTimer {
    public:
        using Duration = std::chrono::duration<double>;
        using Callback = std::function<void(Duration)>;

        explicit ScopedTimer(Callback callback)
            : m_callback(std::move(callback)), m_startedAt(std::chrono::steady_clock::now()) {
        }

        ~ScopedTimer() {
            if (!m_active) {
                return;
            }
            const auto elapsed = std::chrono::duration_cast<Duration>(
                std::chrono::steady_clock::now() - m_startedAt);
            m_callback(elapsed);
        }

        /// Prevents the callback from running.
        void deactivate() {
            m_active = false;
        }

    private:
        bool m_active = true;
        Callback m_callback;
        std::chrono::time_point<std::chrono::steady_clock> m_startedAt;
    };

}

#endif // DSINFER_ONNXDRIVER_SCOPEDTIMER_H
