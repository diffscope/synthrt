#include "GgmlSession.h"

#include <stdcorelib/pimpl.h>

#include <dsinfer/Api/Drivers/Common/CommonDriverApi.h>

#include "GgmlDriver_Logger.h"
#include "internal/Session.h"

namespace ds {

    class GgmlSession::Impl {
    public:
        Impl() {
        }
        ~Impl() {
        }

        int64_t sessionId = 0;
        ggmldriver::Session session;
    };

    static std::atomic<int64_t> s_nextSessionId{1};

    GgmlSession::GgmlSession() : _impl(std::make_unique<Impl>()) {
        _impl->sessionId = s_nextSessionId.fetch_add(1);
    }

    GgmlSession::~GgmlSession() {
        __stdc_impl_t;
    }

    srt::Expected<void> GgmlSession::open(const std::filesystem::path &path,
                                          const srt::NO<InferenceSessionOpenArgs> &args) {
        __stdc_impl_t;
        auto openArgs = args.as<Api::Common::SessionOpenArgs>();
        if (!openArgs) {
            return srt::Error{
                srt::Error::InvalidArgument,
                "session open args is null pointer",
            };
        }
        return impl.session.open(path, openArgs);
    }

    bool GgmlSession::isOpen() const {
        __stdc_impl_t;
        return impl.session.isOpen();
    }

    srt::Expected<void> GgmlSession::close() {
        __stdc_impl_t;
        return impl.session.close();
    }

    int64_t GgmlSession::id() const {
        __stdc_impl_t;
        return impl.sessionId;
    }

    srt::Expected<srt::NO<srt::TaskResult>> GgmlSession::start(const srt::NO<srt::TaskStartInput> &input) {
        __stdc_impl_t;
        return impl.session.run(input);
    }

    srt::Expected<void> GgmlSession::startAsync(const srt::NO<srt::TaskStartInput> &input,
                                                const StartAsyncCallback &callback) {
        return srt::Error(srt::Error::NotImplemented);
    }

    srt::NO<srt::TaskResult> GgmlSession::result() const {
        __stdc_impl_t;
        return impl.session.result();
    }

    bool GgmlSession::stop() {
        return false;
    }

}
