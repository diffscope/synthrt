#include "GgmlDriver.h"

#include <stdcorelib/pimpl.h>
#include <stdcorelib/str.h>

#include <dsinfer/Api/Drivers/Ggml/GgmlDriverApi.h>
#include <dsinfer/Api/Singers/DiffSinger/1/DiffSingerApiL1.h>

#include "GgmlSession.h"
#include "GgmlDriver_Logger.h"

namespace ds {

    using namespace Api;

    namespace ggmldriver {

        srt::LogCategory Log("ggmldriver");

    }

    using ggmldriver::Log;

    class GgmlDriver::Impl {
    public:
        int threads = 4;
        bool useGpu = false;
        bool initialized = false;
    };

    GgmlDriver::GgmlDriver() : _impl(std::make_unique<Impl>()) {
    }

    GgmlDriver::~GgmlDriver() {
    }

    std::string GgmlDriver::arch() const {
        return DiffSinger::L1::API_NAME;
    }

    std::string GgmlDriver::backend() const {
        return Api::Ggml::API_NAME;
    }

    srt::Expected<void> GgmlDriver::initialize(const srt::NO<InferenceDriverInitArgs> &args) {
        __stdc_impl_t;

        if (args->objectName() != Ggml::API_NAME) {
            return srt::Error{
                srt::Error::InvalidArgument,
                stdc::formatN(R"(invalid driver name: expected "%s", got "%s")", Ggml::API_NAME,
                              args->objectName()),
            };
        }

        auto ggmlArgs = args.as<Ggml::DriverInitArgs>();
        if (!ggmlArgs) {
            return srt::Error{srt::Error::InvalidArgument, "ggml args is null pointer"};
        }

        if (impl.initialized) {
            return srt::Error{
                srt::Error::FileDuplicated,
                "ggml driver has already been initialized",
            };
        }

        impl.threads = ggmlArgs->threads;
        impl.useGpu = ggmlArgs->useGpu;
        impl.initialized = true;

        Log.srtInfo("Init - ggml driver initialized (threads=%1, gpu=%2)", impl.threads,
                    impl.useGpu ? "true" : "false");

        return srt::Expected<void>();
    }

    srt::NO<InferenceSession> GgmlDriver::createSession() {
        auto session = srt::NO<GgmlSession>::create();
        return session;
    }

}
