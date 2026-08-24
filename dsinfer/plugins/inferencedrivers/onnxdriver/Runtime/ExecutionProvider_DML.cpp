#include "ExecutionProvider.h"

#include <filesystem>
#include <mutex>
#include <optional>

#ifdef _WIN32
#  include <stdcorelib/platform/windows/stdc_windows.h>
#endif

#if __has_include(<dml_provider_factory.h>)
#  define ONNXDRIVER_FOUND_DML
#  include <dml_provider_factory.h>
#elif defined(ONNXDRIVER_ENABLE_DML)
#  pragma message("dml_provider_factory.h missing, DML support disabled.")
#endif

#include <stdcorelib/support/sharedlibrary.h>

#include <dsinfer/Support/ErrorCode.h>

namespace ds::onnxdriver {

    namespace {

        std::mutex g_libraryPathMutex;

        class DmlLoadGuard {
        public:
            DmlLoadGuard() : m_lock(g_libraryPathMutex) {
                const auto ortPath = stdc::SharedLibrary::locateLibraryPath(&Ort::GetApi());
                if (!ortPath.empty()) {
                    m_originalPath = stdc::SharedLibrary::setLibraryPath(ortPath.parent_path());
                }
            }

            ~DmlLoadGuard() {
                if (m_originalPath) {
                    stdc::SharedLibrary::setLibraryPath(*m_originalPath);
                }
            }

        private:
            std::unique_lock<std::mutex> m_lock;
            std::optional<std::filesystem::path> m_originalPath;
        };

    }

    srt::Expected<void> appendDirectMLExecutionProvider(Ort::SessionOptions &options,
                                                        int deviceIndex) {
#if defined(ONNXDRIVER_ENABLE_DML) && defined(ONNXDRIVER_FOUND_DML)
        if (!options) {
            return srt::Error(srt::Error::InvalidArgument,
                              "ONNX Runtime session options must not be null");
        }
        if (deviceIndex < 0) {
            return srt::Error(srt::Error::InvalidArgument,
                              "DirectML device index must be non-negative");
        }

        const OrtApi &ortApi = Ort::GetApi();
        const OrtDmlApi *ortDmlApi;
        Ort::Status getApiStatus((ortApi.GetExecutionProviderApi(
            "DML", ORT_API_VERSION, reinterpret_cast<const void **>(&ortDmlApi))));
        if (!getApiStatus.IsOK()) {
            return srt::Error(ds::ErrorCode::DriverLoadFailed, getApiStatus.GetErrorMessage());
        }

        options.DisableMemPattern();
        options.SetExecutionMode(ORT_SEQUENTIAL);

        DmlLoadGuard dmlLoadGuard;
        Ort::Status appendStatus(
            ortDmlApi->SessionOptionsAppendExecutionProvider_DML(options, deviceIndex));
        if (!appendStatus.IsOK()) {
            return srt::Error(ds::ErrorCode::DriverLoadFailed, appendStatus.GetErrorMessage());
        }
        return {};
#else
        return srt::Error(srt::Error::FeatureNotSupported,
                          "the ONNX driver was built without DirectML support");
#endif
    }

}
