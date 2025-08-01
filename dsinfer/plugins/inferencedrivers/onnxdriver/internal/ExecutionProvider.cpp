#include "ExecutionProvider.h"

#include <memory>
#include <filesystem>
#include <optional>

#if __has_include(<dml_provider_factory.h>)
#  define ONNXDRIVER_FOUND_DML
#  include <dml_provider_factory.h>
#elif defined(ONNXDRIVER_ENABLE_DML)
#  pragma message("dml_provider_factory.h missing, DML support disabled.")
#endif

#include <stdcorelib/support/sharedlibrary.h>

namespace ds::onnxdriver {

    bool initCUDA(Ort::SessionOptions &options, int deviceIndex, std::string *errorMessage) {
#if defined(ONNXDRIVER_ENABLE_CUDA)
        if (!options) {
            if (errorMessage) {
                *errorMessage = "SessionOptions must not be nullptr!";
            }
            return false;
        }

        if (deviceIndex < 0) {
            if (errorMessage) {
                *errorMessage = "GPU device index must be a non-negative integer!";
            }
            return false;
        }

        const OrtApi &ortApi = Ort::GetApi();

        OrtCUDAProviderOptionsV2 *cudaOptionsPtr = nullptr;
        Ort::Status createStatus(ortApi.CreateCUDAProviderOptions(&cudaOptionsPtr));

        // Currently, ORT C++ API does not have a wrapper for CUDAProviderOptionsV2.
        // Let the smart pointer take ownership of cudaOptionsPtr so it will be released when it
        // goes out of scope.
        std::unique_ptr<OrtCUDAProviderOptionsV2, decltype(ortApi.ReleaseCUDAProviderOptions)>
            cudaOptions(cudaOptionsPtr, ortApi.ReleaseCUDAProviderOptions);

        if (!createStatus.IsOK()) {
            if (errorMessage) {
                *errorMessage = createStatus.GetErrorMessage();
            }
            return false;
        }

        // The following block of code sets device_id
        {
            // Device ID from int to string
            auto cudaDeviceIdStr = std::to_string(deviceIndex);
            auto cudaDeviceIdCStr = cudaDeviceIdStr.c_str();

            constexpr int CUDA_OPTIONS_SIZE = 2;
            const char *cudaOptionsKeys[CUDA_OPTIONS_SIZE] = {"device_id",
                                                              "cudnn_conv_algo_search"};
            const char *cudaOptionsValues[CUDA_OPTIONS_SIZE] = {cudaDeviceIdCStr, "DEFAULT"};
            Ort::Status updateStatus(ortApi.UpdateCUDAProviderOptions(
                cudaOptions.get(), cudaOptionsKeys, cudaOptionsValues, CUDA_OPTIONS_SIZE));
            if (!updateStatus.IsOK()) {
                if (errorMessage) {
                    *errorMessage = updateStatus.GetErrorMessage();
                }
                return false;
            }
        }
        Ort::Status appendStatus(
            ortApi.SessionOptionsAppendExecutionProvider_CUDA_V2(options, cudaOptions.get()));
        if (!appendStatus.IsOK()) {
            if (errorMessage) {
                *errorMessage = appendStatus.GetErrorMessage();
            }
            return false;
        }
        return true;
#else
        if (errorMessage) {
            *errorMessage = "The library is not built with CUDA support.";
        }
        return false;
#endif
    }

    struct DmlLoadGuard {
        DmlLoadGuard() {
            static bool dmlLoaded = false;
            if (!dmlLoaded) {
                auto ortPath = stdc::SharedLibrary::locateLibraryPath(&Ort::GetApi());
                if (!ortPath.empty()) {
                    orgDllPath = stdc::SharedLibrary::setLibraryPath(ortPath.parent_path());
                }
            }
        }

        ~DmlLoadGuard() {
            if (orgDllPath) {
                stdc::SharedLibrary::setLibraryPath(orgDllPath.value());
            }
        }

        std::optional<std::filesystem::path> orgDllPath;
    };

    bool initDirectML(Ort::SessionOptions &options, int deviceIndex, std::string *errorMessage) {
#if defined(ONNXDRIVER_ENABLE_DML) && defined(ONNXDRIVER_FOUND_DML)
        if (!options) {
            if (errorMessage) {
                *errorMessage = "SessionOptions must not be nullptr!";
            }
            return false;
        }

        if (deviceIndex < 0) {
            if (errorMessage) {
                *errorMessage = "GPU device index must be a non-negative integer!";
            }
            return false;
        }

        const OrtApi &ortApi = Ort::GetApi();
        const OrtDmlApi *ortDmlApi;
        Ort::Status getApiStatus((ortApi.GetExecutionProviderApi(
            "DML", ORT_API_VERSION, reinterpret_cast<const void **>(&ortDmlApi))));
        if (!getApiStatus.IsOK()) {
            // Failed to get DirectML API.
            if (errorMessage) {
                *errorMessage = getApiStatus.GetErrorMessage();
            }
            return false;
        }

        // Successfully get DirectML API
        options.DisableMemPattern();
        options.SetExecutionMode(ORT_SEQUENTIAL);

        DmlLoadGuard dmlLoadGuard;
        Ort::Status appendStatus(
            ortDmlApi->SessionOptionsAppendExecutionProvider_DML(options, deviceIndex));
        if (!appendStatus.IsOK()) {
            if (errorMessage) {
                *errorMessage = appendStatus.GetErrorMessage();
            }
            return false;
        }
        return true;
#else
        if (errorMessage) {
            *errorMessage = "The library is not built with DirectML support.";
        }
        return false;
#endif
    }

}