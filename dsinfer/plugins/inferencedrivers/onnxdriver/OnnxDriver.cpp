#include "OnnxDriver.h"

#include <stdcorelib/pimpl.h>
#include <stdcorelib/str.h>
#include <stdcorelib/support/sharedlibrary.h>

#include <dsinfer/Api/Drivers/Onnx/OnnxDriverApi.h>
#include <dsinfer/Api/Singers/DiffSinger/1/DiffSingerApiL1.h>

#include "OnnxSession.h"
#include "OnnxDriver_Logger.h"
#include "internal/Env.h"

#ifndef ORT_API_MANUAL_INIT
#  error "dsinfer requires ort to be manually initialized, but ORT_API_MANUAL_INIT is not set!"
#endif

#include <onnxruntime_cxx_api.h>

#if defined(_WIN32)
#  define ONNXRUNTIME_DYLIB_FILENAME _TSTR("onnxruntime.dll")
#elif defined(__APPLE__)
#  define ONNXRUNTIME_DYLIB_FILENAME _TSTR("libonnxruntime.dylib")
#else
#  define ONNXRUNTIME_DYLIB_FILENAME _TSTR("libonnxruntime.so")
#endif

namespace fs = std::filesystem;

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

        srt::Expected<void> load(const fs::path &path) {
            Log.srtInfo("Init - Loading onnx environment");

            auto dylib = std::make_unique<stdc::SharedLibrary>();

            /**
             *  1. Load Ort shared library and create handle
             */
            Log.srtDebug("Init - Loading ORT shared library from %1", path);
#ifdef _WIN32
            auto orgLibPath = stdc::SharedLibrary::setLibraryPath(path.parent_path());
#endif
            if (!dylib->open(path, stdc::SharedLibrary::ResolveAllSymbolsHint)) {
                std::string msg =
                    stdc::formatN("Load library failed: %1 [%2]", dylib->lastError(), path);
                Log.srtCritical("Init - %1", msg);
                return srt::Error(srt::Error::SessionError, std::move(msg));
            }
#ifdef _WIN32
            stdc::SharedLibrary::setLibraryPath(orgLibPath);
#endif

            /**
             *  2. Get Ort API getter handle
             */
            Log.srtDebug("Init - Getting ORT API handle");
            auto handle =
                reinterpret_cast<OrtApiBase *(ORT_API_CALL *) ()>(dylib->resolve("OrtGetApiBase"));
            if (!handle) {
                std::string msg =
                    stdc::formatN("Failed to get API handle: %1 [%2]", dylib->lastError(), path);
                Log.srtCritical("Init - %1", msg);
                return srt::Error(srt::Error::SessionError, std::move(msg));
            }

            /**
             *  3. Check Ort API
             */
            Log.srtDebug("Init - ORT_API_VERSION is %1", ORT_API_VERSION);
            auto apiBase = handle();
            auto api = apiBase->GetApi(ORT_API_VERSION);
            if (!api) {
                std::string msg = stdc::formatN("%1: failed to get API instance");
                Log.srtCritical("Init - %1", msg);
                return srt::Error(srt::Error::SessionError, std::move(msg));
            }
            Log.srtDebug("Init - ORT library version is %1", apiBase->GetVersionString());

            /**
             *  4. Successfully get Ort API
             */
            Ort::InitApi(api);

            ortDSO.swap(dylib);

            loaded = true;
            ortPath = path;
            ortApiBase = apiBase;
            ortApi = api;

            Log.srtInfo("Init - Onnx environment Load successful");
            return srt::Expected<void>();
        }

        std::unique_ptr<stdc::SharedLibrary> ortDSO;

        // Metadata
        bool loaded = false;
        fs::path ortPath;

        // Library data
        void *hLibrary = nullptr;
        const OrtApi *ortApi = nullptr;
        const OrtApiBase *ortApiBase = nullptr;
    };

    OnnxDriver::OnnxDriver() : _impl(std::make_unique<Impl>()) {
    }

    OnnxDriver::~OnnxDriver() {
    }

    std::string OnnxDriver::arch() const {
        return DiffSinger::L1::API_NAME;
    }

    std::string OnnxDriver::backend() const {
        return Api::Onnx::API_NAME;
    }

    srt::Expected<void> OnnxDriver::initialize(const srt::NO<InferenceDriverInitArgs> &args) {
        __stdc_impl_t;

        if (args->objectName() != Onnx::API_NAME) {
            return srt::Error{
                srt::Error::InvalidArgument,
                stdc::formatN(R"(invalid driver name: expected "%s", got "%s")", Onnx::API_NAME,
                              args->objectName()),
            };
        }

        auto onnxArgs = args.as<Onnx::DriverInitArgs>();
        if (!onnxArgs) {
            return srt::Error{srt::Error::InvalidArgument, "onnx args is null pointer"};
        }

        // Example logging
        Log.srtDebug("initialize: driver name: %1", args->objectName());

        if (impl.loaded) {
            return srt::Error{
                srt::Error::FileDuplicated,
                "onnx runtime has been initialized by another instance",
            };
        }

        auto dllPath = onnxArgs->runtimePath / ONNXRUNTIME_DYLIB_FILENAME;

        if (!impl.load(dllPath)) {
            return srt::Error{
                srt::Error::SessionError,
                "failed to load onnx runtime library",
            };
        }

        onnxdriver::Env::DeviceConfig devConfig;
        devConfig.ep = onnxArgs->ep;
        devConfig.deviceIndex = onnxArgs->deviceIndex;
        onnxdriver::Env::setDeviceConfig(devConfig);
        return srt::Expected<void>();
    }

    srt::NO<InferenceSession> OnnxDriver::createSession() {
        auto session = srt::NO<OnnxSession>::create();
        return session;
    }

}