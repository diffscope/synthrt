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
            return std::move(srt::core::Error(
                srt::core::ErrorCode::InvalidArgument,
                "getInferenceDriver: runtime is nullptr")
                .withTrace(std::source_location::current(), "ExtractorDriver::getInferenceDriver"));
        }

        auto *cate = runtime->moduleCategory("inference");
        if (!cate) {
            return std::move(srt::core::Error(
                srt::core::ErrorCode::ExtractNotInitialized,
                "getInferenceDriver: 'inference' module category not found; "
                "runtime is not initialized with an inference module")
                .withTrace(std::source_location::current(), "ExtractorDriver::getInferenceDriver"));
        }

        auto obj = cate->getFirstObject("dsdriver");
        if (!obj) {
            return std::move(srt::core::Error(
                srt::core::ErrorCode::DriverNotFound,
                "getInferenceDriver: 'dsdriver' not registered in inference category")
                .withTrace(std::source_location::current(), "ExtractorDriver::getInferenceDriver"));
        }

        auto driver = obj.as<srt::driver::InferenceDriver>();
        if (!driver) {
            return std::move(srt::core::Error(
                srt::core::ErrorCode::DriverNotFound,
                "getInferenceDriver: 'dsdriver' is not an InferenceDriver")
                .withTrace(std::source_location::current(), "ExtractorDriver::getInferenceDriver"));
        }

        // Only check backend, not arch (extractors are not DiffSinger models).
        if (driver->backend() != srt::driver::onnx::API_NAME) {
            return std::move(srt::core::Error(
                srt::core::ErrorCode::DriverUnsupportedProvider,
                "getInferenceDriver: dsdriver backend is '" + driver->backend() +
                    "', expected '" + srt::driver::onnx::API_NAME + "'")
                .withTrace(std::source_location::current(), "ExtractorDriver::getInferenceDriver"));
        }

        return driver;
    }

} // namespace srt::extract
