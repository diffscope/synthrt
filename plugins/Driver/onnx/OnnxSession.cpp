#include "OnnxSession.h"

#include <stdcorelib/pimpl.h>

#include "internal/Env.h"
#include "internal/Session.h"

namespace srt::driver::onnx {

    class OnnxSession::Impl {
    public:
        Impl() : sessionId(Env::nextId()) {
        }
        ~Impl() {
        }

        int64_t sessionId;
        ::srt::driver::onnx::Session session;
    };

    OnnxSession::OnnxSession() : _impl(std::make_unique<Impl>()) {
    }

    OnnxSession::~OnnxSession() {
        __stdc_impl_t;
    }

    srt::core::Expected<void>
        OnnxSession::open(const std::filesystem::path &path,
                          const srt::core::NO<srt::driver::InferenceSessionOpenArgs> &args) {
        __stdc_impl_t;
        auto openArgs = args.as<SessionOpenArgs>();
        if (!openArgs) {
            return srt::core::Error{
                srt::core::Error::InvalidArgument,
                "session open args is null pointer",
            };
        }
        return impl.session.open(path, openArgs);
    }

    bool OnnxSession::isOpen() const {
        __stdc_impl_t;
        return impl.session.isOpen();
    }

    srt::core::Expected<void> OnnxSession::close() {
        __stdc_impl_t;
        return impl.session.close();
    }

    int64_t OnnxSession::id() const {
        __stdc_impl_t;
        return impl.sessionId;
    }

    std::vector<std::string> OnnxSession::inputNames() const {
        __stdc_impl_t;
        return impl.session.inputNames();
    }

    srt::core::Expected<srt::core::NO<srt::core::TaskResult>>
        OnnxSession::start(const srt::core::NO<srt::core::TaskStartInput> &input) {
        __stdc_impl_t;
        return impl.session.run(input);
    }

    srt::core::Expected<void>
        OnnxSession::startAsync(const srt::core::NO<srt::core::TaskStartInput> &input,
                                const srt::core::ITask::StartAsyncCallback &callback) {
        __stdc_impl_t;
        return impl.session.runAsync(input, callback);
    }

    srt::core::NO<srt::core::TaskResult> OnnxSession::result() const {
        __stdc_impl_t;
        return impl.session.result();
    }

    bool OnnxSession::stop() {
        __stdc_impl_t;
        impl.session.terminate();
        return true;
    }

}
