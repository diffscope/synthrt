#include "inferutil/Driver.h"

#include <stdcorelib/str.h>
#include <synthrt/Core/SynthUnit.h>

#include <dsinfer/Support/ErrorCode.h>
#include <dsinfer/Api/Drivers/Onnx/OnnxDriverApi.h>

namespace ds::inferutil {
    srt::Expected<InferenceDriver *> getInferenceDriver(const srt::Inference *obj) {
        namespace Onnx = Api::Onnx;
        auto *service = obj->synthUnit().runtimeService(InferenceDriver::IID, Onnx::API_NAME);
        if (!service) {
            return srt::Error(ds::ErrorCode::NotInitialized,
                              "could not find the ONNX inference driver");
        }
        auto *onnxDriver = service->as<InferenceDriver>();

        const auto arch = onnxDriver->arch();
        constexpr auto expectedArch = "diffsinger";
        const bool isArchMatch = arch == expectedArch;

        const auto backend = onnxDriver->backend();
        constexpr auto expectedBackend = Onnx::API_NAME;
        const bool isBackendMatch = backend == expectedBackend;

        if (!isArchMatch || !isBackendMatch) {
            return srt::Error(
                ds::ErrorCode::DriverMismatch,
                stdc::formatN(
                    R"(invalid driver: expected arch "%1", got "%2" (%3); expected backend "%4", got "%5" (%6))",
                    expectedArch, arch, (isArchMatch ? "match" : "MISMATCH"), expectedBackend,
                    backend, (isBackendMatch ? "match" : "MISMATCH")));
        }

        return onnxDriver;
    }
}
