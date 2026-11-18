#include "OnnxSession.h"

#include <utility>

#include <dsinfer/Support/ErrorCode.h>

#include "Runtime/DriverContext.h"
#include "Session/Session.h"

namespace ds {

    OnnxSession::OnnxSession(std::shared_ptr<onnxdriver::DriverContext> context)
        : m_sessionId(context->nextSessionId()),
          m_session(std::make_unique<onnxdriver::Session>(std::move(context))) {
    }

    OnnxSession::~OnnxSession() {
        (void) stop();
        (void) waitForFinished();
    }

    srt::Expected<void> OnnxSession::open(const std::filesystem::path &path,
                                          const InferenceSessionOpenArgs &args) {
        if (args.type() != Api::Onnx::Name || args.version() != Api::Onnx::Version) {
            return srt::Error{
                srt::Error::InvalidArgument,
                "invalid session open arguments",
            };
        }
        return m_session->open(path, *args.as<Api::Onnx::SessionOpenArgs>());
    }

    bool OnnxSession::isOpen() const {
        return m_session->isOpen();
    }

    srt::Expected<void> OnnxSession::close() {
        return m_session->close();
    }

    int64_t OnnxSession::id() const {
        return m_sessionId;
    }

    srt::Expected<std::unique_ptr<srt::TaskResult>>
        OnnxSession::execute(const srt::TaskStartInput &input) {
        return m_session->run(input);
    }

    srt::Expected<void> OnnxSession::quit() {
        if (m_session->isRunning()) {
            m_session->terminate();
        }
        return {};
    }

    srt::Expected<void> OnnxSession::wait() {
        m_session->waitForFinished();
        return {};
    }

    OnnxSession::State OnnxSession::classifyOutcome(
        const srt::Expected<std::unique_ptr<srt::TaskResult>> &outcome) noexcept {
        if (!outcome && stopRequested()) {
            return Canceled;
        }
        return outcome ? Succeeded : Failed;
    }

}
