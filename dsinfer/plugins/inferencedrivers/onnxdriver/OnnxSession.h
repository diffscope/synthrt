#ifndef DSINFER_ONNXSESSION_H
#define DSINFER_ONNXSESSION_H

#include <cstdint>
#include <memory>

#include <dsinfer/Inference/InferenceSession.h>

namespace ds {

    namespace onnxdriver {
        class DriverContext;
        class Session;
    }

    /// Adapts an internal ONNX session to the public inference task interface.
    class OnnxSession : public InferenceSession {
    public:
        explicit OnnxSession(std::shared_ptr<onnxdriver::DriverContext> context);
        ~OnnxSession();

        /// Opens the ONNX model at \a path.
        srt::Expected<void> open(const std::filesystem::path &path,
                                 const InferenceSessionOpenArgs &args) override;

        /// Waits for active execution and releases the opened model.
        srt::Expected<void> close() override;

        /// Returns whether a model is open.
        bool isOpen() const override;

        /// Returns the identifier assigned by the owning driver context.
        int64_t id() const override;

        /// Runs inference synchronously.
        srt::Expected<std::unique_ptr<srt::TaskResult>>
            start(const srt::TaskStartInput &input) override;

        /// Starts inference and invokes \a callback when execution finishes.
        srt::Expected<void> startAsync(std::shared_ptr<const srt::TaskStartInput> input,
                                       AsyncCallback callback) override;

        /// Requests termination of active inference.
        srt::Expected<void> stop() override;

        /// Waits until active inference and its callback finish.
        srt::Expected<void> waitForFinished() override;

    private:
        // Atomically enters Running unless another execution is active.
        srt::Expected<void> beginExecution();

        int64_t m_sessionId;
        std::unique_ptr<onnxdriver::Session> m_session;
    };

}

#endif // DSINFER_ONNXSESSION_H
