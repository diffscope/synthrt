#include "OnnxDriver.h"

#include <stdcorelib/pimpl.h>
#include <stdcorelib/str.h>
#include <stdcorelib/support/sharedlibrary.h>

#include <synthrt/Driver/onnx/OnnxDriverApi.h>
#include <synthrt/Driver/onnx/OnnxTensor.h>

#include "OnnxSession.h"
#include "OnnxDriver_Logger.h"
#include "internal/Env.h"

#ifndef ORT_API_MANUAL_INIT
#  error "srt-driver requires ort to be manually initialized, but ORT_API_MANUAL_INIT is not set!"
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

namespace srt::driver::onnx {

    srt::LogCategory Log("onnxdriver");

    class OnnxDriver::Impl {
    public:
        Impl() {
        }

        ~Impl() {
        }

        srt::core::Expected<void> load(const fs::path &path) {
            Log.srtInfo("Init - Loading onnx environment");

            auto dylib = std::make_unique<stdc::SharedLibrary>();

            /**
             *  1. Load Ort shared library and create handle
             */
            Log.srtDebug("Init - Loading ORT shared library from %1", path);
#ifdef _WIN32
            auto orgLibPath = stdc::SharedLibrary::setLibraryPath(path.parent_path());
#endif
            if (!dylib->open(path, stdc::SharedLibrary::ResolveAllSymbolsHint |
                                       stdc::SharedLibrary::ExportExternalSymbolsHint)) {
                std::string msg =
                    stdc::formatN("Load library failed: %1 [%2]", dylib->lastError(), path);
                Log.srtCritical("Init - %1", msg);
#ifdef _WIN32
                stdc::SharedLibrary::setLibraryPath(orgLibPath);
#endif
                return std::move(srt::core::Error(srt::core::Error::SessionError, std::move(msg))
                    .withTrace(std::source_location::current(), "OnnxDriver::Impl::load"));
            }
#ifdef _WIN32
            stdc::SharedLibrary::setLibraryPath(orgLibPath);
#endif
            /**
             *  2. Get Ort Api getter handle
             */
            Log.srtDebug("Init - Getting ORT API handle");
            auto handle =
                reinterpret_cast<OrtApiBase *(ORT_API_CALL *) ()>(dylib->resolve("OrtGetApiBase"));
            if (!handle) {
                std::string msg =
                    stdc::formatN("Failed to get API handle: %1 [%2]", dylib->lastError(), path);
                Log.srtCritical("Init - %1", msg);
                return std::move(srt::core::Error(srt::core::Error::SessionError, std::move(msg))
                    .withTrace(std::source_location::current(), "OnnxDriver::Impl::load"));
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
                return std::move(srt::core::Error(srt::core::Error::SessionError, std::move(msg))
                    .withTrace(std::source_location::current(), "OnnxDriver::Impl::load"));
            }
            Log.srtDebug("Init - ORT library version is %1", apiBase->GetVersionString());

            /**
             *  4. Successfully get Ort API
             */
            Ort::InitApi(api);
            // Also initialize the ORT API pointer for srt-driver.dll (which has its own
            // copy of Global<void>::api_ due to ORT_API_MANUAL_INIT and DLL separation).
            srt::driver::onnx::initOnnxRuntimeApi(api);

            ortDSO.swap(dylib);

            loaded = true;
            ortPath = path;
            ortApiBase = apiBase;
            ortApi = api;

            Log.srtInfo("Init - Onnx environment Load successful");
            return srt::core::Expected<void>();
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
        // DiffSinger singer architecture.
        return "diffsinger";
    }

    std::string OnnxDriver::backend() const {
        return API_NAME;
    }

    srt::core::Expected<void>
        OnnxDriver::initialize(const srt::core::NO<srt::driver::InferenceDriverInitArgs> &args) {
        __stdc_impl_t;

        if (args->objectName() != API_NAME) {
            return std::move(srt::core::Error{
                srt::core::Error::InvalidArgument,
                stdc::formatN(R"(invalid driver name: expected "%s", got "%s")", API_NAME,
                              args->objectName()),
            }.withTrace(std::source_location::current(), "OnnxDriver::initialize"));
        }

        auto onnxArgs = args.as<DriverInitArgs>();
        if (!onnxArgs) {
            return std::move(srt::core::Error{srt::core::Error::InvalidArgument,
                                    "onnx args is null pointer"}
                .withTrace(std::source_location::current(), "OnnxDriver::initialize"));
        }

        // Example logging
        Log.srtDebug("initialize: driver name: %1", args->objectName());

        if (impl.loaded) {
            return std::move(srt::core::Error{
                srt::core::Error::FileDuplicated,
                "onnx runtime has been initialized by another instance",
            }.withTrace(std::source_location::current(), "OnnxDriver::initialize"));
        }

        auto dllPath = onnxArgs->runtimePath / ONNXRUNTIME_DYLIB_FILENAME;

        auto loadExp = impl.load(dllPath);
        if (!loadExp) {
            return std::move(loadExp.takeError()
                .withTrace(std::source_location::current(), "OnnxDriver::initialize"));
        }

        Env::DeviceConfig devConfig;
        devConfig.ep = onnxArgs->ep;
        devConfig.deviceIndex = onnxArgs->deviceIndex;
        Env::setDeviceConfig(devConfig);
        return srt::core::Expected<void>();
    }

    srt::core::NO<srt::driver::InferenceSession> OnnxDriver::createSession() {
        auto session = srt::core::NO<OnnxSession>::create();
        return session;
    }

}
