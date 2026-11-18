#ifndef DSINFER_INFERENCEDRIVERS_ONNXDRIVER_ONNXSESSION_H
#define DSINFER_INFERENCEDRIVERS_ONNXDRIVER_ONNXSESSION_H

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

    protected:
        srt::Expected<std::unique_ptr<srt::TaskResult>>
            execute(const srt::TaskStartInput &input, const srt::TaskStopToken &token) override;

        srt::Expected<void> quit() override;

        srt::Expected<void> wait() override;

        State classifyOutcome(const srt::Expected<std::unique_ptr<srt::TaskResult>> &outcome,
                              bool stopRequested) noexcept override;

    private:
        int64_t m_sessionId;
        std::unique_ptr<onnxdriver::Session> m_session;
    };

}

#endif // DSINFER_INFERENCEDRIVERS_ONNXDRIVER_ONNXSESSION_H
