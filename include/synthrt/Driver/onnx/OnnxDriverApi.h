#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <set>

#include <synthrt/Core/Tensor/ITensor.h>
#include <synthrt/Driver/InferenceDriver.h>
#include <synthrt/Driver/InferenceSession.h>

namespace srt::driver::onnx {

    inline constexpr char API_NAME[] = "onnx";

    inline constexpr int API_VERSION = 1;

    enum ExecutionProvider {
        CPUExecutionProvider = 0,
        CUDAExecutionProvider,
        DMLExecutionProvider,
        CoreMLExecutionProvider,
    };

    class DriverInitArgs : public srt::driver::InferenceDriverInitArgs {
    public:
        inline DriverInitArgs() : InferenceDriverInitArgs(API_NAME, API_VERSION) {
        }

        /// The execution provider to use.
        ExecutionProvider ep = CPUExecutionProvider;

        /// The device index to use for CUDAExecutionProvider. (-1 means auto-select)
        int deviceIndex = -1;

        /// The onnxruntime library directory. (empty means use the default)
        std::filesystem::path runtimePath;
    };

    class SessionOpenArgs : public srt::driver::InferenceSessionOpenArgs {
    public:
        inline SessionOpenArgs() : InferenceSessionOpenArgs(API_NAME, API_VERSION) {
        }

        /// Whether to force the use of the CPU for the session. (highest priority)
        bool useCpu = false;

        /// Optional per-session execution provider override. When set (and
        /// \c useCpu is false), this session uses the specified EP instead of
        /// the driver's global EP, enabling different callers (G2P, inference,
        /// ...) to share one ONNX driver while pinning their own EP.
        std::optional<ExecutionProvider> ep;

        /// Optional per-session device index override. Used together with
        /// \c ep. Falls back to the driver's global deviceIndex when unset.
        std::optional<int> deviceIndex;
    };

    class SessionStartInput : public srt::driver::InferenceSessionStartInput {
    public:
        inline SessionStartInput() : InferenceSessionStartInput(API_NAME, API_VERSION) {
        }

        /// The input port names and the input tensors.
        std::map<std::string, srt::core::NO<srt::core::ITensor>> inputs;

        /// The output port names.
        std::set<std::string> outputs;
    };

    class SessionResult : public srt::driver::InferenceSessionResult {
    public:
        inline SessionResult() : InferenceSessionResult(API_NAME, API_VERSION) {
        }

        std::map<std::string, srt::core::NO<srt::core::ITensor>> outputs;
    };

}
