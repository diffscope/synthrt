#include "OnnxDriver.h"

#include <stdcorelib/pimpl.h>

#include "OnnxSession.h"

namespace ds {

    class OnnxDriver::Impl {
    public:
        Impl() {
        }

        ~Impl() {
        }
    };

    OnnxDriver::OnnxDriver() : _impl(std::make_unique<Impl>()) {
    }

    OnnxDriver::~OnnxDriver() {
    }

    bool OnnxDriver::initialize(const srt::NO<InferenceDriverInitArgs> &args, srt::Error *error) {
        __stdc_impl_t;
        return true;
    }

    InferenceSession *OnnxDriver::createSession() {
        return new OnnxSession();
    }

}