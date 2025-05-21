#include "SessionImage.h"

#include <onnxruntime_cxx_api.h>

#include <dsinfer/Api/Drivers/Onnx/OnnxDriverApi.h>

#include "OnnxDriver_Logger.h"
#include "ExecutionProvider.h"
#include "Env.h"
#include "Session.h"

namespace ds::onnxdriver {
    using Api::Onnx::ExecutionProvider;

    static Ort::Session createOrtSession(const Ort::Env &ortEnv,
                                         const std::filesystem::path &modelPath,
                                         bool preferCpu,
                                         std::string *errorMessage) {
        auto devConfig = Env::getDeviceConfig();
        auto ep = devConfig.ep;
        auto deviceIndex = devConfig.deviceIndex;
        try {
            Ort::SessionOptions sessOpt;

            std::string initEPErrorMsg;
            if (!preferCpu) {
                switch (ep) {
                    case ExecutionProvider::DMLExecutionProvider: {
                        if (!initDirectML(sessOpt, deviceIndex, &initEPErrorMsg)) {
                            // log warning: "Could not initialize DirectML: {initEPErrorMsg},
                            // falling back to CPU."
                            Log.srtWarning(
                                "Could not initialize DirectML: %1, falling back to CPU.",
                                initEPErrorMsg);
                        } else {
                            Log.srtInfo("Use DirectML. Device index: %1", deviceIndex);
                        }
                        break;
                    }
                    case ExecutionProvider::CUDAExecutionProvider: {
                        if (!initCUDA(sessOpt, deviceIndex, &initEPErrorMsg)) {
                            // log warning: "Could not initialize CUDA: {initEPErrorMsg}, falling
                            // back to CPU."
                            Log.srtWarning(
                                "Could not initialize CUDA: %1, falling back to CPU.",
                                initEPErrorMsg);
                        } else {
                            Log.srtInfo("Use CUDA. Device index: %1", deviceIndex);
                        }
                        break;
                    }
                    default: {
                        // log info: "Use CPU."
                        Log.srtInfo("Use CPU.");
                        break;
                    }
                }
            } else {
                Log.srtInfo("The model prefers to use CPU. [%1]", modelPath.filename());
            }
            return Ort::Session{ortEnv, std::filesystem::path::string_type(modelPath).c_str(),
                                sessOpt};
        } catch (const Ort::Exception &e) {
            if (errorMessage) {
                *errorMessage = e.what();
            }
        }
        return Ort::Session{nullptr};
    }

    static void loggingFuncOrt(void *param, OrtLoggingLevel severity, const char *category,
                               const char *logid, const char *code_location, const char *message) {
        switch (severity) {
            case ORT_LOGGING_LEVEL_VERBOSE:
                Log.srtLog(Debug, "[%1] %2", code_location, message);
                break;
            case ORT_LOGGING_LEVEL_WARNING:
                Log.srtLog(Warning, "[%1] %2", code_location, message);
                break;
            case ORT_LOGGING_LEVEL_ERROR:
                Log.srtLog(Critical, "[%1] %2", code_location, message);
                break;
            case ORT_LOGGING_LEVEL_FATAL:
                Log.srtLog(Fatal, "[%1] %2", code_location, message);
                break;
            default:
                Log.srtLog(Information, "[%1] %2", code_location, message);
                break;
        }
    }

    SessionImage::SessionImage()
        : env(ORT_LOGGING_LEVEL_WARNING, "dsinfer", loggingFuncOrt, nullptr), session(nullptr) {
    }

    SessionImage::~SessionImage() = default;

    bool SessionImage::open(const std::filesystem::path &onnxPath, int hints,
                            std::string *errorMessage) {
        auto filename = onnxPath.filename();
        Log.srtDebug("SessionImage [%1] - creating", filename);

        session = createOrtSession(env, onnxPath, hints & Session::SH_PreferCPUHint, errorMessage);
        if (!session) {
            Log.srtCritical("SessionImage [%1] - create failed", filename);
            return false;
        }
        Ort::AllocatorWithDefaultOptions allocator;

        auto inputCount = session.GetInputCount();
        inputNames.reserve(inputCount);
        for (size_t i = 0; i < inputCount; ++i) {
            inputNames.emplace_back(session.GetInputNameAllocated(i, allocator).get());
        }

        auto outputCount = session.GetOutputCount();
        outputNames.reserve(outputCount);
        for (size_t i = 0; i < outputCount; ++i) {
            outputNames.emplace_back(session.GetOutputNameAllocated(i, allocator).get());
        }
        Log.srtDebug("SessionImage [%1] - created successfully", filename);
        return true;
    }

}