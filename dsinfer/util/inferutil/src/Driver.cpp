#include "inferutil/Driver.h"

#include <synthrt/Core/SynthUnit.h>

#include <dsinfer/Support/ErrorCode.h>
#include <dsinfer/Api/Drivers/Onnx/OnnxDriverApi.h>

namespace ds::inferutil {
    srt::Expected<InferenceDriver *> getInferenceDriver(const srt::InferenceExecutive *obj) {
        namespace Onnx = Api::Onnx;
        auto service = obj->synthUnit().runtimeService(InferenceDriver::IID, Onnx::API_NAME);
        if (!service) {
            return srt::Error(ds::ErrorCode::NotInitialized,
                              "could not find the ONNX inference driver");
        }
        auto onnxDriver = service->as<InferenceDriver>();

        const auto backend = onnxDriver->backend();
        constexpr auto expectedBackend = Onnx::API_NAME;
        if (backend != expectedBackend) {
            return srt::Error(ds::ErrorCode::DriverMismatch,
                              "inference driver does not implement the ONNX backend contract");
        }

        return onnxDriver;
    }
}
