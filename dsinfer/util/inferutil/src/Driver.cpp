#include "inferutil/Driver.h"

#include <stdcorelib/str.h>
#include <dsinfer/Support/ErrorCode.h>
#include <dsinfer/Api/Drivers/Onnx/OnnxDriverApi.h>
#include <dsinfer/Api/Singers/DiffSinger/1/DiffSingerApiL1.h>

namespace ds::inferutil {
    srt::Expected<InferenceDriver *> getInferenceDriver(const srt::Inference *obj) {
        namespace Onnx = Api::Onnx;
        namespace DiffSinger = Api::DiffSinger::L1;

        auto inferenceCate = obj->spec()->SU()->category("inference");
        auto dsdriverObject = inferenceCate->getFirstUniqueObject("dsdriver");

        if (!dsdriverObject) {
            return srt::Error(ds::ErrorCode::NotInitialized, "could not find dsdriver");
        }

        auto onnxDriver = static_cast<InferenceDriver *>(dsdriverObject);

        const auto arch = onnxDriver->arch();
        constexpr auto expectedArch = DiffSinger::API_NAME;
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