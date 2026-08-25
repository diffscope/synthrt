#include "OnnxRuntime.h"

#include <mutex>
#include <system_error>
#include <utility>

#include <stdcorelib/str.h>
#include <stdcorelib/support/sharedlibrary.h>

#include <dsinfer/Support/ErrorCode.h>

#include "OnnxDriverLogging.h"

#ifndef ORT_API_MANUAL_INIT
#  error "dsinfer requires ORT_API_MANUAL_INIT"
#endif

namespace ds::onnxdriver {

    namespace {

        std::mutex g_runtimeMutex;
        std::weak_ptr<OnnxRuntime> g_activeRuntime;

        std::filesystem::path normalizedPath(const std::filesystem::path &path) {
            std::error_code error;
            auto result = std::filesystem::weakly_canonical(path, error);
            if (!error) {
                return result;
            }
            result = std::filesystem::absolute(path, error);
            return (error ? path : result).lexically_normal();
        }

        void ortLog(void *, OrtLoggingLevel severity, const char *, const char *,
                    const char *codeLocation, const char *message) {
            switch (severity) {
                case ORT_LOGGING_LEVEL_VERBOSE:
                    g_log.srtDebug("[%1] %2", codeLocation, message);
                    break;
                case ORT_LOGGING_LEVEL_WARNING:
                    g_log.srtWarning("[%1] %2", codeLocation, message);
                    break;
                case ORT_LOGGING_LEVEL_ERROR:
                    g_log.srtCritical("[%1] %2", codeLocation, message);
                    break;
                case ORT_LOGGING_LEVEL_FATAL:
                    g_log.srtFatal("[%1] %2", codeLocation, message);
                    break;
                default:
                    g_log.srtInfo("[%1] %2", codeLocation, message);
                    break;
            }
        }

    }

    OnnxRuntime::OnnxRuntime() : m_environment(nullptr) {
    }

    OnnxRuntime::~OnnxRuntime() = default;

    srt::Expected<std::shared_ptr<OnnxRuntime>>
        OnnxRuntime::load(const std::filesystem::path &path) {
        if (path.empty()) {
            return srt::Error(srt::Error::InvalidArgument,
                              "ONNX Runtime library path must not be empty");
        }

        std::lock_guard<std::mutex> lock(g_runtimeMutex);
        if (auto active = g_activeRuntime.lock()) {
            if (!active->path().empty() && normalizedPath(active->path()) == normalizedPath(path)) {
                return active;
            }
            return srt::Error(ds::ErrorCode::DriverLoadFailed,
                              "a different ONNX Runtime API is already active in this plugin");
        }

        auto runtime = std::shared_ptr<OnnxRuntime>(new OnnxRuntime());
        if (auto result = runtime->initializeLoaded(path); !result) {
            return result.takeError();
        }
        g_activeRuntime = runtime;
        return runtime;
    }

    srt::Expected<std::shared_ptr<OnnxRuntime>>
        OnnxRuntime::borrow(const Api::Onnx::RuntimeApi &runtimeApi) {
        if (!runtimeApi.ortApiBase || !runtimeApi.ortApi) {
            return srt::Error(srt::Error::InvalidArgument,
                              "external ONNX Runtime API pointers must not be null");
        }
        if (runtimeApi.ortApiVersion != ORT_API_VERSION) {
            return srt::Error(srt::Error::InvalidArgument,
                              stdc::formatN("external ONNX Runtime API version must be %1, got %2",
                                            ORT_API_VERSION, runtimeApi.ortApiVersion));
        }
        if (runtimeApi.ortApiBase->GetApi(runtimeApi.ortApiVersion) != runtimeApi.ortApi) {
            return srt::Error(srt::Error::InvalidArgument,
                              "external ONNX Runtime API does not belong to its API base");
        }

        std::lock_guard<std::mutex> lock(g_runtimeMutex);
        if (auto active = g_activeRuntime.lock()) {
            if (active->api().ortApi == runtimeApi.ortApi) {
                return active;
            }
            return srt::Error(ds::ErrorCode::DriverLoadFailed,
                              "a different ONNX Runtime API is already active in this plugin");
        }

        auto runtime = std::shared_ptr<OnnxRuntime>(new OnnxRuntime());
        if (auto result = runtime->initializeBorrowed(runtimeApi); !result) {
            return result.takeError();
        }
        g_activeRuntime = runtime;
        return runtime;
    }

    const Api::Onnx::RuntimeApi &OnnxRuntime::api() const noexcept {
        return m_api;
    }

    const std::filesystem::path &OnnxRuntime::path() const noexcept {
        return m_path;
    }

    Ort::Env &OnnxRuntime::environment() noexcept {
        return m_environment;
    }

    srt::Expected<void> OnnxRuntime::initializeLoaded(const std::filesystem::path &path) {
        auto library = std::make_unique<stdc::SharedLibrary>();
        const auto hints = stdc::SharedLibrary::ResolveAllSymbolsHint |
                           stdc::SharedLibrary::ExportExternalSymbolsHint |
                           stdc::SharedLibrary::SearchLibraryLoadDirectoryHint;
        if (!library->open(path, hints)) {
            return srt::Error(ds::ErrorCode::DriverLoadFailed,
                              stdc::formatN("failed to load ONNX Runtime: %1 [%2]",
                                            library->errorMessage(), path));
        }

        using GetApiBase = const OrtApiBase *(ORT_API_CALL *) ();
        auto getApiBase = reinterpret_cast<GetApiBase>(library->resolve("OrtGetApiBase"));
        if (!getApiBase) {
            return srt::Error(ds::ErrorCode::DriverLoadFailed,
                              stdc::formatN("failed to resolve OrtGetApiBase: %1 [%2]",
                                            library->errorMessage(), path));
        }

        const auto apiBase = getApiBase();
        const auto api = apiBase ? apiBase->GetApi(ORT_API_VERSION) : nullptr;
        if (!api) {
            return srt::Error(ds::ErrorCode::DriverLoadFailed,
                              stdc::formatN("ONNX Runtime does not provide API version %1 [%2]",
                                            ORT_API_VERSION, path));
        }

        m_library = std::move(library);
        m_api = {apiBase, api, ORT_API_VERSION};
        m_path = normalizedPath(path);
        return initializeEnvironment();
    }

    srt::Expected<void> OnnxRuntime::initializeBorrowed(const Api::Onnx::RuntimeApi &runtimeApi) {
        m_api = runtimeApi;
        return initializeEnvironment();
    }

    srt::Expected<void> OnnxRuntime::initializeEnvironment() {
        Ort::InitApi(m_api.ortApi);
        try {
            m_environment = Ort::Env(ORT_LOGGING_LEVEL_WARNING, "synthrt-dsinfer", ortLog, nullptr);
        } catch (const Ort::Exception &error) {
            return srt::Error(ds::ErrorCode::DriverLoadFailed,
                              std::string("failed to create ONNX Runtime environment: ") +
                                  error.what());
        }
        return {};
    }

}
