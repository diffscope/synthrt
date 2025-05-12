#include "OnnxDriver.h"

#include <stdcorelib/pimpl.h>
#include <stdcorelib/str.h>
#include <stdcorelib/support/sharedlibrary.h>

#include <dsinfer/Api/Drivers/Onnx/OnnxDriverApi.h>

#include "OnnxSession.h"
#include "OnnxDriver_Logger.h"

namespace ds {

    using namespace Api;

    namespace onnxdriver {

        srt::LogCategory Log("onnxdriver");

    }

    using onnxdriver::Log;

    class OnnxDriver::Impl {
    public:
        Impl() {
        }

        ~Impl() {
        }

        // TODO: 

        std::unique_ptr<stdc::SharedLibrary> ortDSO;
    };

    OnnxDriver::OnnxDriver() : _impl(std::make_unique<Impl>()) {
    }

    OnnxDriver::~OnnxDriver() {
    }

    bool OnnxDriver::initialize(const srt::NO<InferenceDriverInitArgs> &args, srt::Error *error) {
        __stdc_impl_t;

        if (args->objectName() != Onnx::API_NAME) {
            *error = {
                srt::Error::InvalidArgument,
                stdc::formatN(R"(invalid driver name: expected "%s", got "%s")", Onnx::API_NAME,
                              args->objectName()),
            };
            return false;
        }

        auto onnxArgs = args.as<Onnx::DriverInitArgs>();

        // Example logging
        Log.srtDebug("initialize: driver name: %1", args->objectName());
        return true;
    }

    srt::NO<InferenceSession> OnnxDriver::createSession() {
        return srt::NO<InferenceSession>::create();
    }

}