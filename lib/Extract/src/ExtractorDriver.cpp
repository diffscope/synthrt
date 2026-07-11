// ExtractorDriver.cpp - ONNX inference driver discovery for extractors.
//
// Migrated from ds-editor-lite RmvpeModel.cpp:13-44. Unlike the original,
// this version only checks backend == "onnx" and does NOT check arch
// (extractors are not DiffSinger models).

#include <synthrt/Extract/ExtractorDriver.h>

#include <string>

#include <synthrt/Core/Core/NamedObject.h>
#include <synthrt/Core/Core/Runtime.h>
#include <synthrt/Core/Module/Module.h>
#include <synthrt/Core/Support/Error.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Driver/InferenceDriver.h>
#include <synthrt/Driver/onnx/OnnxDriverApi.h>

namespace srt::extract {

    srt::core::Expected<srt::core::NO<srt::driver::InferenceDriver>>
    getInferenceDriver(const srt::core::Runtime *runtime) {
        if (!runtime) {
            return srt::core::Error(
                srt::core::ErrorCode::SessionError, "Runtime is nullptr");
        }

        auto *cate = runtime->moduleCategory("inference");
        if (!cate) {
            return srt::core::Error(
                srt::core::ErrorCode::DriverNotFound, "inference category not found");
        }

        auto obj = cate->getFirstObject("dsdriver");
        if (!obj) {
            return srt::core::Error(
                srt::core::ErrorCode::DriverNotFound, "dsdriver not found");
        }

        auto driver = obj.as<srt::driver::InferenceDriver>();

        // Only check backend, not arch (extractors are not DiffSinger models).
        if (driver->backend() != srt::driver::onnx::API_NAME) {
            return srt::core::Error(
                srt::core::ErrorCode::DriverNotFound, "backend is not onnx");
        }

        return driver;
    }

} // namespace srt::extract
