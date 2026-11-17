#include "OnnxDriver.h"

#include <stdcorelib/str.h>

#include <dsinfer/Api/Drivers/Onnx/OnnxDriverApi.h>

#include "OnnxDriverLogging.h"
#include "OnnxSession.h"
#include "Runtime/DriverContext.h"
#include "Runtime/OnnxRuntime.h"

#ifndef ORT_API_MANUAL_INIT
#  error "dsinfer requires ort to be manually initialized, but ORT_API_MANUAL_INIT is not set!"
#endif

#include <onnxruntime_cxx_api.h>

namespace ds {

    using namespace Api;

    namespace onnxdriver {

        srt::LogCategory g_log("onnxdriver");

    }

    OnnxDriver::OnnxDriver() : InferenceDriver(Api::Onnx::Name) {
    }

    OnnxDriver::~OnnxDriver() = default;

    srt::Expected<void> OnnxDriver::initialize(const InferenceDriverInitArgs &args) {
        if (args.type() != Onnx::Name || args.version() != Onnx::Version) {
            return srt::Error{
                srt::Error::InvalidArgument,
                stdc::formatN(
                    R"(invalid driver payload: expected "%1" level %2, got "%3" level %4)",
                    Onnx::Name, Onnx::Version, args.type(), args.version()),
            };
        }

        const auto &onnxArgs = *args.as<Onnx::DriverInitArgs>();

        if (m_context) {
            return srt::Error{
                srt::Error::FileDuplicated,
                "ONNX driver is already initialized",
            };
        }

        if (onnxArgs.ep == Onnx::ExecutionProvider::CoreML) {
            return srt::Error(srt::Error::FeatureNotSupported,
                              "the ONNX driver does not implement the Core ML provider");
        }

        srt::Expected<std::shared_ptr<onnxdriver::OnnxRuntime>> runtime =
            onnxArgs.runtimeApi
                ? onnxdriver::OnnxRuntime::borrow(*onnxArgs.runtimeApi)
                : onnxdriver::OnnxRuntime::load(onnxArgs.runtimePath / ONNXRUNTIME_DYLIB_FILENAME);
        if (!runtime) {
            return runtime.takeError().withContext("failed to initialize ONNX Runtime");
        }

        m_context = std::make_shared<onnxdriver::DriverContext>(runtime.take(), onnxArgs.ep,
                                                                onnxArgs.deviceIndex);
        m_extension.runtimeApi = m_context->runtime().api();
        m_extension.runtimePath = m_context->runtime().path();
        m_extension.ep = onnxArgs.ep;
        m_extension.deviceIndex = onnxArgs.deviceIndex;

        onnxdriver::g_log.srtInfo("Initialized ONNX Runtime %1",
                                  m_extension.runtimeApi.ortApiBase->GetVersionString());
        return {};
    }

    std::unique_ptr<InferenceSession> OnnxDriver::createSession() {
        return m_context ? std::make_unique<OnnxSession>(m_context) : nullptr;
    }

    const InferenceDriverExtension *OnnxDriver::extension() const {
        return m_context ? &m_extension : nullptr;
    }

}
