#include "OnnxDriver.h"

#include <stdcorelib/pimpl.h>
#include <stdcorelib/str.h>
#include <stdcorelib/support/sharedlibrary.h>

#include <dsinfer/Support/ErrorCode.h>
#include <dsinfer/Api/Drivers/Onnx/OnnxDriverApi.h>

#include "OnnxSession.h"
#include "OnnxDriver_Logger.h"
#include "internal/Env.h"

#ifndef ORT_API_MANUAL_INIT
#  error "dsinfer requires ort to be manually initialized, but ORT_API_MANUAL_INIT is not set!"
#endif

#include <onnxruntime_cxx_api.h>

#if defined(_WIN32)
#  define ONNXRUNTIME_DYLIB_FILENAME STDC_TSTR("onnxruntime.dll")
#elif defined(__APPLE__)
#  define ONNXRUNTIME_DYLIB_FILENAME STDC_TSTR("libonnxruntime.dylib")
#else
#  define ONNXRUNTIME_DYLIB_FILENAME STDC_TSTR("libonnxruntime.so")
#endif

namespace fs = std::filesystem;

namespace ds {

    using namespace Api;

    namespace onnxdriver {

        srt::LogCategory Log("onnxdriver");

    }

    using onnxdriver::Log;

#ifdef _WIN32
    /// Scoped override of the process-wide DLL search path.
    ///
    /// The ORT directory must be searchable while \c onnxruntime.dll is being opened, so that its
    /// own dependencies resolve. The previous path is restored on every exit path, including the
    /// error ones.
    class ScopedLibraryPath {
    public:
        explicit ScopedLibraryPath(const fs::path &dir)
            : _original(stdc::SharedLibrary::setLibraryPath(dir)) {
        }
        ~ScopedLibraryPath() {
            stdc::SharedLibrary::setLibraryPath(_original);
        }

        ScopedLibraryPath(const ScopedLibraryPath &) = delete;
        ScopedLibraryPath &operator=(const ScopedLibraryPath &) = delete;

    private:
        fs::path _original;
    };
#endif

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
            {
#ifdef _WIN32
                ScopedLibraryPath libraryPath(path.parent_path());
#endif
                if (!dylib->open(path, stdc::SharedLibrary::ResolveAllSymbolsHint |
                                           stdc::SharedLibrary::ExportExternalSymbolsHint)) {
                    std::string msg =
                        stdc::formatN("Load library failed: %1 [%2]", dylib->errorMessage(), path);
                    Log.srtCritical("Init - %1", msg);
                    return srt::Error(ds::ErrorCode::DriverLoadFailed, std::move(msg));
                }
            }

            /**
             *  2. Get Ort API getter handle
             */
            Log.srtDebug("Init - Getting ORT API handle");
            auto handle =
                reinterpret_cast<OrtApiBase *(ORT_API_CALL *) ()>(dylib->resolve("OrtGetApiBase"));
            if (!handle) {
                std::string msg =
                    stdc::formatN("Failed to get API handle: %1 [%2]", dylib->errorMessage(), path);
                Log.srtCritical("Init - %1", msg);
                return srt::Error(ds::ErrorCode::DriverLoadFailed, std::move(msg));
            }

            /**
             *  3. Check Ort API
             */
            Log.srtDebug("Init - ORT_API_VERSION is %1", ORT_API_VERSION);
            auto apiBase = handle();
            auto api = apiBase->GetApi(ORT_API_VERSION);
            if (!api) {
                std::string msg = stdc::formatN("%1: failed to get API instance", path);
                Log.srtCritical("Init - %1", msg);
                return srt::Error(ds::ErrorCode::DriverLoadFailed, std::move(msg));
            }
            Log.srtDebug("Init - ORT library version is %1", apiBase->GetVersionString());

            /**
             *  4. Successfully get Ort API
             */
            Ort::InitApi(api);

            ortDSO.swap(dylib);

            extension.runtimePath = path;
            extension.ortApiBase = apiBase;
            extension.ortApi = api;
            extension.ortApiVersion = ORT_API_VERSION;

            Log.srtInfo("Init - Onnx environment Load successful");
            return srt::Expected<void>();
        }

        /// Keeps the library the extension points into alive.
        std::unique_ptr<stdc::SharedLibrary> ortDSO;

        /// Everything a caller may ask about the runtime, and the record of whether it is loaded:
        /// \c ortApi is null until load() succeeds.
        Api::Onnx::DriverExtension extension;
    };

    OnnxDriver::OnnxDriver()
        : InferenceDriver(Api::Onnx::API_NAME), _impl(std::make_unique<Impl>()) {
    }

    OnnxDriver::~OnnxDriver() {
    }

    srt::Expected<void> OnnxDriver::initialize(const InferenceDriverInitArgs &args) {
        stdc_impl_t;

        if (args.type() != Onnx::API_NAME || args.version() != Onnx::API_VERSION) {
            return srt::Error{
                srt::Error::InvalidArgument,
                stdc::formatN(
                    R"(invalid driver payload: expected "%1" level %2, got "%3" level %4)",
                    Onnx::API_NAME, Onnx::API_VERSION, args.type(), args.version()),
            };
        }

        const auto &onnxArgs = *args.as<Onnx::DriverInitArgs>();

        // Example logging
        Log.srtDebug("initialize: driver type: %1", args.type());

        if (impl.extension.ortApi) {
            return srt::Error{
                srt::Error::FileDuplicated,
                "onnx runtime has been initialized by another instance",
            };
        }

        auto dllPath = onnxArgs.runtimePath / ONNXRUNTIME_DYLIB_FILENAME;

        if (auto exp = impl.load(dllPath); !exp) {
            // Propagate the detailed reason from \c load() instead of a generic message.
            return exp;
        }

        onnxdriver::Env::DeviceConfig devConfig;
        devConfig.ep = onnxArgs.ep;
        devConfig.deviceIndex = onnxArgs.deviceIndex;
        onnxdriver::Env::setDeviceConfig(devConfig);

        impl.extension.ep = onnxArgs.ep;
        impl.extension.deviceIndex = onnxArgs.deviceIndex;
        return srt::Expected<void>();
    }

    std::unique_ptr<InferenceSession> OnnxDriver::createSession() {
        return std::make_unique<OnnxSession>();
    }

    const InferenceDriverExtension *OnnxDriver::extension() const {
        stdc_impl_t;
        return impl.extension.ortApi ? &impl.extension : nullptr;
    }

}
