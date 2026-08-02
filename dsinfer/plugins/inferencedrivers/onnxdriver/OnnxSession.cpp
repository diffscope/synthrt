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
                                          const srt::NO<InferenceSessionOpenArgs> &args) {
        stdc_impl_t;
        auto openArgs = args.as<Api::Onnx::SessionOpenArgs>();
        if (!openArgs) {
            return srt::Error{
                srt::Error::InvalidArgument,
                "session open args is null pointer",
            };
        }
        return impl.session.open(path, openArgs);
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

    srt::Expected<srt::NO<srt::TaskResult>> OnnxSession::start(const srt::NO<srt::TaskStartInput> &input) {
        stdc_impl_t;
        return impl.session.run(input);
    }

    srt::Expected<void> OnnxSession::startAsync(const srt::NO<srt::TaskStartInput> &input,
                                                const StartAsyncCallback &callback) {
        stdc_impl_t;
        return impl.session.runAsync(input, callback);
    }

    srt::NO<srt::TaskResult> OnnxSession::result() const {
        stdc_impl_t;
        return impl.session.result();
    }

    bool OnnxSession::stop() {
        stdc_impl_t;
        impl.session.terminate();
        return true;
    }

}