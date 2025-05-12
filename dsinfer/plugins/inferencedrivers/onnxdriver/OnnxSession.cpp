#include "OnnxSession.h"

#include <stdcorelib/pimpl.h>

namespace ds {

    class OnnxSession::Impl {
    public:
        Impl() {
        }
        ~Impl() {
        }
    };

    OnnxSession::OnnxSession() : _impl(std::make_unique<Impl>()) {
    }

    OnnxSession::~OnnxSession() {
        __stdc_impl_t;
    }

    bool OnnxSession::open(const std::filesystem::path &path,
                           const srt::NO<InferenceSessionOpenArgs> &args, srt::Error *error) {
        __stdc_impl_t;
        // TODO: 
        return false;
    }

    bool OnnxSession::isOpen() const {
        __stdc_impl_t;
        // TODO: 
        return false;
    }

    bool OnnxSession::close(srt::Error *error) {
        __stdc_impl_t;
        // TODO: 
        return false;
    }

    int64_t OnnxSession::id() const {
        __stdc_impl_t;
        // TODO: 
        return 0;
    }

    bool OnnxSession::start(const srt::NO<srt::TaskStartInput> &input, srt::Error *error) {
        __stdc_impl_t;
        // TODO: 
        return false;
    }

    bool OnnxSession::startAsync(const srt::NO<srt::TaskStartInput> &input,
                                 const StartAsyncCallback &callback, srt::Error *error) {
        // TODO: 
        return false;
    }

    srt::NO<srt::TaskResult> OnnxSession::result() const {
        // TODO: 
        return {};
    }

    bool OnnxSession::stop() {
        // TODO: 
        return false;
    }

}