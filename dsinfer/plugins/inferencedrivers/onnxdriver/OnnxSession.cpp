#include "OnnxSession.h"

#include <stdcorelib/pimpl.h>

#include "internal/Env.h"
#include "internal/Session.h"

namespace ds {

    class OnnxSession::Impl {
    public:
        Impl() : sessionId(onnxdriver::Env::nextId()) {
        }
        ~Impl() {
        }

        int64_t sessionId;
        onnxdriver::Session session;
    };

    OnnxSession::OnnxSession() : _impl(std::make_unique<Impl>()) {
    }

    OnnxSession::~OnnxSession() {
        stdc_impl_t;
    }

    srt::Expected<void> OnnxSession::open(const std::filesystem::path &path,
                                          const InferenceSessionOpenArgs &args) {
        stdc_impl_t;
        if (args.type() != Api::Onnx::API_NAME || args.version() != Api::Onnx::API_VERSION) {
            return srt::Error{
                srt::Error::InvalidArgument,
                "invalid session open arguments",
            };
        }
        return impl.session.open(path, *args.as<Api::Onnx::SessionOpenArgs>());
    }

    bool OnnxSession::isOpen() const {
        stdc_impl_t;
        return impl.session.isOpen();
    }

    srt::Expected<void> OnnxSession::close() {
        stdc_impl_t;
        return impl.session.close();
    }

    int64_t OnnxSession::id() const {
        stdc_impl_t;
        return impl.sessionId;
    }

    srt::Expected<std::unique_ptr<srt::TaskResult>>
        OnnxSession::start(const srt::TaskStartInput &input) {
        stdc_impl_t;
        m_state = Running;
        auto result = impl.session.run(input);
        m_state = result ? Succeeded : Failed;
        return result;
    }

    srt::Expected<void> OnnxSession::startAsync(std::shared_ptr<const srt::TaskStartInput> input,
                                                AsyncCallback callback) {
        stdc_impl_t;
        m_state = Running;
        auto result = impl.session.runAsync(
            std::move(input), [this, callback = std::move(callback)](auto result) mutable {
                m_state = result ? Succeeded : Failed;
                callback(std::move(result));
            });
        if (!result) {
            m_state = Failed;
        }
        return result;
    }

    srt::Expected<void> OnnxSession::stop() {
        stdc_impl_t;
        impl.session.terminate();
        m_state = Canceled;
        return {};
    }

    srt::Expected<void> OnnxSession::waitForFinished() {
        stdc_impl_t;
        impl.session.waitForFinished();
        return {};
    }

}
