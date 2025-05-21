#include "OnnxDriver.h"

#include <stdcorelib/pimpl.h>
#include <stdcorelib/str.h>
#include <stdcorelib/support/sharedlibrary.h>

#include <dsinfer/Api/Drivers/Onnx/OnnxDriverApi.h>

#include "OnnxSession.h"
#include "OnnxDriver_Logger.h"
#include "internal/Env.h"

#ifndef ORT_API_MANUAL_INIT
# error "dsinfer requires ort to be manually initialized, but ORT_API_MANUAL_INIT is not set!"
#endif

#include <onnxruntime_cxx_api.h>

#if defined(_WIN32)
# define ONNXRUNTIME_DYLIB_FILENAME _TSTR("onnxruntime.dll")
#elif defined(__APPLE__)
# define ONNXRUNTIME_DYLIB_FILENAME _TSTR("libonnxruntime.dylib")
#else
# define ONNXRUNTIME_DYLIB_FILENAME _TSTR("libonnxruntime.so")
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

        bool load(const fs::path &path, srt::Error *error) {
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
                std::string msg = stdc::formatN("Load library failed: %1 [%2]", dylib->lastError(), path);
                Log.srtCritical("Init - %1", msg);
                if (error) {
                    *error = srt::Error(srt::Error::SessionError, std::move(msg));
                }
                return false;
            }
#ifdef _WIN32
            stdc::SharedLibrary::setLibraryPath(orgLibPath);
#endif

            /**
             *  2. Get Ort API getter handle
             */
            Log.srtDebug("Init - Getting ORT API handle");
            auto handle = static_cast<OrtApiBase *(ORT_API_CALL *) ()>(dylib->resolve("OrtGetApiBase"));
            if (!handle) {
                std::string msg = stdc::formatN("Failed to get API handle: %1 [%2]", dylib->lastError(), path);
                Log.srtCritical("Init - %1", msg);
                if (error) {
                    *error = srt::Error(srt::Error::SessionError, std::move(msg));
                }
                return false;
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
                if (error) {
                    *error = srt::Error(srt::Error::SessionError, std::move(msg));
                }
                return false;
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
            return true;
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

    bool OnnxDriver::initialize(const srt::NO<InferenceDriverInitArgs> &args, srt::Error *error) {
        __stdc_impl_t;

        if (args->objectName() != Onnx::API_NAME) {
            if (error) {
                *error = {
                    srt::Error::InvalidArgument,
                    stdc::formatN(R"(invalid driver name: expected "%s", got "%s")", Onnx::API_NAME,
                                  args->objectName()),
                };
            }
            return false;
        }

        auto onnxArgs = args.as<Onnx::DriverInitArgs>();
        if (!onnxArgs) {
            if (error) {
                *error = {
                    srt::Error::InvalidArgument,
                    "onnx args is null pointer"
                };
            }
            return false;
        }

        // Example logging
        Log.srtDebug("initialize: driver name: %1", args->objectName());

        if (impl.loaded) {
            if (error) {
                *error = {
                    srt::Error::FileDuplicated,
                    "onnx runtime has been initialized by another instance",
                };
            }
            return false;
        }

        auto dllPath = onnxArgs->runtimePath / ONNXRUNTIME_DYLIB_FILENAME;

        if (!impl.load(dllPath, error)) {
            if (error) {
                *error = {
                    srt::Error::SessionError,
                    "failed to load onnx runtime library",
                };
            }
            return false;
        }

        onnxdriver::Env::DeviceConfig devConfig;
        devConfig.ep = onnxArgs->ep;
        devConfig.deviceIndex = onnxArgs->deviceIndex;
        onnxdriver::Env::setDeviceConfig(devConfig);

        return true;
    }

    srt::NO<InferenceSession> OnnxDriver::createSession() {
        auto session = srt::NO<OnnxSession>::create();
        return session;
    }

}