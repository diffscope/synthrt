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
        __stdc_impl_t;
    }

    bool OnnxSession::open(const std::filesystem::path &path,
                           const srt::NO<InferenceSessionOpenArgs> &args, srt::Error *error) {
        __stdc_impl_t;
        auto openArgs = args.as<Api::Onnx::SessionOpenArgs>();
        if (!openArgs) {
            if (error) {
                *error = {
                    srt::Error::InvalidArgument,
                    "session open args is null pointer"
                };
            }
            return false;
        }
        return impl.session.open(path, openArgs, error);
    }

    bool OnnxSession::isOpen() const {
        __stdc_impl_t;
        return impl.session.isOpen();
    }

    bool OnnxSession::close(srt::Error *error) {
        __stdc_impl_t;
        return impl.session.close();
    }

    int64_t OnnxSession::id() const {
        __stdc_impl_t;
        return impl.sessionId;
    }

    bool OnnxSession::start(const srt::NO<srt::TaskStartInput> &input, srt::Error *error) {
        __stdc_impl_t;
        return impl.session.run(input, error);
    }

    bool OnnxSession::startAsync(const srt::NO<srt::TaskStartInput> &input,
                                 const StartAsyncCallback &callback, srt::Error *error) {
        __stdc_impl_t;
        return impl.session.runAsync(input, callback, error);
    }

    srt::NO<srt::TaskResult> OnnxSession::result() const {
        __stdc_impl_t;
        return impl.session.result();
    }

    bool OnnxSession::stop() {
        __stdc_impl_t;
        impl.session.terminate();
        return true;
    }

}