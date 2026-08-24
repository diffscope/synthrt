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

    OnnxSession::~OnnxSession() = default;

    srt::Expected<void> OnnxSession::open(const std::filesystem::path &path,
                                          const InferenceSessionOpenArgs &args) {
        if (args.type() != Api::Onnx::API_NAME || args.version() != Api::Onnx::API_VERSION) {
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
        OnnxSession::start(const srt::TaskStartInput &input) {
        if (auto begun = beginExecution(); !begun) {
            return begun.takeError();
        }
        auto result = m_session->run(input);
        if (state() != Canceled) {
            setState(result ? Succeeded : Failed);
        }
        return result;
    }

    srt::Expected<void> OnnxSession::startAsync(std::shared_ptr<const srt::TaskStartInput> input,
                                                AsyncCallback callback) {
        if (!callback) {
            return srt::Error(srt::Error::InvalidArgument,
                              "asynchronous callback must not be empty");
        }

        if (auto begun = beginExecution(); !begun) {
            return begun.takeError();
        }
        auto result = m_session->runAsync(
            std::move(input), [this, callback = std::move(callback)](auto result) mutable {
                if (state() != Canceled) {
                    setState(result ? Succeeded : Failed);
                }
                callback(std::move(result));
            });
        if (!result) {
            setState(Failed);
        }
        return result;
    }

    srt::Expected<void> OnnxSession::stop() {
        if (state() != Running) {
            return srt::Error(ds::ErrorCode::NotInitialized, "the ONNX session is not running");
        }
        m_session->terminate();
        setState(Canceled);
        return {};
    }

    srt::Expected<void> OnnxSession::waitForFinished() {
        m_session->waitForFinished();
        return {};
    }

    srt::Expected<void> OnnxSession::beginExecution() {
        if (m_session->isRunning()) {
            return srt::Error(ds::ErrorCode::SessionFailed, "the ONNX session is already running");
        }

        auto previous = m_state.load();
        do {
            if (previous == Running) {
                return srt::Error(ds::ErrorCode::SessionFailed,
                                  "the ONNX session is already running");
            }
        } while (!m_state.compare_exchange_weak(previous, Running));
        return {};
    }

}
