#include "inferutil/Driver.h"

#include <stdcorelib/str.h>

#include <synthrt/Core/Core/Runtime.h>
#include <synthrt/Core/Module/Module.h>
#include <synthrt/SVS/Inference.h>
#include <synthrt/Driver/onnx/OnnxDriverApi.h>
#include <diffsinger/Infer/dsinfer/Api/Singers/DiffSinger/1/DiffSingerApiL1.h>

namespace ds::infer::inferutil {
    srt::core::Expected<srt::core::NO<srt::driver::InferenceDriver>> getInferenceDriver(const srt::svs::Inference *obj) {
        namespace Onnx = srt::driver::onnx;
        namespace DiffSinger = srt::svs::Api::DiffSinger::L1;

        auto inferenceCate = obj->spec()->runtime()->moduleCategory("inference");
        if (!inferenceCate) {
            return srt::core::Error(srt::core::Error::SessionError,
                              "[InferUtil] could not find inference module category");
        }
        auto dsdriverObject = inferenceCate->getFirstObject("dsdriver");

        if (!dsdriverObject) {
            return srt::core::Error(srt::core::Error::SessionError,
                              "[InferUtil] could not find dsdriver object in inference category");
        }

        auto onnxDriver = dsdriverObject.as<srt::driver::InferenceDriver>();

        const auto arch = onnxDriver->arch();
        constexpr auto expectedArch = DiffSinger::API_NAME;
        const bool isArchMatch = arch == expectedArch;

        const auto backend = onnxDriver->backend();
        constexpr auto expectedBackend = Onnx::API_NAME;
        const bool isBackendMatch = backend == expectedBackend;

        if (!isArchMatch || !isBackendMatch) {
            return srt::core::Error(
                srt::core::Error::SessionError,
                stdc::formatN(
                    R"([InferUtil] invalid driver: expected arch "%1", got "%2" (%3); expected backend "%4", got "%5" (%6))",
                    expectedArch, arch, (isArchMatch ? "match" : "MISMATCH"), expectedBackend,
                    backend, (isBackendMatch ? "match" : "MISMATCH")));
        }

        return onnxDriver;
    }
}
