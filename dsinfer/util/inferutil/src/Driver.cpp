#include "inferutil/Driver.h"

#include <stdcorelib/str.h>
#include <dsinfer/Api/Drivers/Onnx/OnnxDriverApi.h>
#include <dsinfer/Api/Singers/DiffSinger/1/DiffSingerApiL1.h>

namespace ds::inferutil {
    srt::Expected<srt::NO<InferenceDriver>> getInferenceDriver(const srt::Inference *obj) {
        namespace Onnx = Api::Onnx;
        namespace DiffSinger = Api::DiffSinger::L1;

        auto inferenceCate = obj->spec()->SU()->category("inference");
        auto dsdriverObject = inferenceCate->getFirstObject("dsdriver");

        if (!dsdriverObject) {
            return srt::Error(srt::Error::SessionError, "could not find dsdriver");
        }

        auto onnxDriver = dsdriverObject.as<InferenceDriver>();

        const auto arch = onnxDriver->arch();
        constexpr auto expectedArch = DiffSinger::API_NAME;
        const bool isArchMatch = arch == expectedArch;

        const auto backend = onnxDriver->backend();
        constexpr auto expectedBackend = Onnx::API_NAME;
        const bool isBackendMatch = backend == expectedBackend;

        if (!isArchMatch || !isBackendMatch) {
            return srt::Error(
                srt::Error::SessionError,
                stdc::formatN(
                    R"(invalid driver: expected arch "%1", got "%2" (%3); expected backend "%4", got "%5" (%6))",
                    expectedArch, arch, (isArchMatch ? "match" : "MISMATCH"), expectedBackend,
                    backend, (isBackendMatch ? "match" : "MISMATCH")));
        }

        return onnxDriver;
    }
}