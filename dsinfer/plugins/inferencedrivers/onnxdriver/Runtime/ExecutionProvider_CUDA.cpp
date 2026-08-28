#include "ExecutionProvider.h"

#include <array>
#include <memory>
#include <string>

#include <dsinfer/Support/ErrorCode.h>

namespace ds::onnxdriver {

    srt::Expected<void> appendCudaExecutionProvider(Ort::SessionOptions &options, int deviceIndex) {
#if defined(ONNXDRIVER_ENABLE_CUDA)
        if (!options) {
            return srt::Error(srt::Error::InvalidArgument,
                              "ONNX Runtime session options must not be null");
        }
        if (deviceIndex < 0) {
            return srt::Error(srt::Error::InvalidArgument,
                              "CUDA device index must be non-negative");
        }

        const OrtApi &ortApi = Ort::GetApi();

        OrtCUDAProviderOptionsV2 *cudaOptionsPtr = nullptr;
        Ort::Status createStatus(ortApi.CreateCUDAProviderOptions(&cudaOptionsPtr));

        // ORT does not provide a C++ owner for CUDAProviderOptionsV2. Keep the C object under
        // automatic ownership as soon as CreateCUDAProviderOptions returns it.
        std::unique_ptr<OrtCUDAProviderOptionsV2, decltype(ortApi.ReleaseCUDAProviderOptions)>
            cudaOptions(cudaOptionsPtr, ortApi.ReleaseCUDAProviderOptions);

        if (!createStatus.IsOK()) {
            return srt::Error(ds::ErrorCode::DriverLoadFailed, createStatus.GetErrorMessage());
        }

        const auto cudaDeviceId = std::to_string(deviceIndex);
        constexpr std::array optionKeys = {"device_id", "cudnn_conv_algo_search"};
        const std::array optionValues = {cudaDeviceId.c_str(), "HEURISTIC"};
        static_assert(std::tuple_size<decltype(optionKeys)>::value ==
                      std::tuple_size<decltype(optionValues)>::value);
        Ort::Status updateStatus(ortApi.UpdateCUDAProviderOptions(
            cudaOptions.get(), optionKeys.data(), optionValues.data(), optionKeys.size()));
        if (!updateStatus.IsOK()) {
            return srt::Error(ds::ErrorCode::DriverLoadFailed, updateStatus.GetErrorMessage());
        }

        Ort::Status appendStatus(
            ortApi.SessionOptionsAppendExecutionProvider_CUDA_V2(options, cudaOptions.get()));
        if (!appendStatus.IsOK()) {
            return srt::Error(ds::ErrorCode::DriverLoadFailed, appendStatus.GetErrorMessage());
        }
        return {};
#else
        return srt::Error(srt::Error::FeatureNotSupported,
                          "the ONNX driver was built without CUDA support");
#endif
    }

}
