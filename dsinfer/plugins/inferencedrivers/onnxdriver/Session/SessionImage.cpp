#include "SessionImage.h"

#include "OnnxDriverLogging.h"
#include "Runtime/DriverContext.h"
#include "Runtime/ExecutionProvider.h"

namespace ds::onnxdriver {
    using Api::Onnx::ExecutionProvider;

    static srt::Expected<Ort::Session> createOrtSession(DriverContext &context,
                                                        const std::filesystem::path &modelPath,
                                                        bool preferCpu) {
        const auto ep = context.executionProvider();
        const auto deviceIndex = context.deviceIndex();
        try {
            Ort::SessionOptions sessionOptions;

            if (!preferCpu) {
                srt::Expected<void> providerResult;
                switch (ep) {
                    case ExecutionProvider::DML: {
                        providerResult =
                            appendDirectMLExecutionProvider(sessionOptions, deviceIndex);
                        break;
                    }
                    case ExecutionProvider::CUDA: {
                        providerResult = appendCudaExecutionProvider(sessionOptions, deviceIndex);
                        break;
                    }
                    default:
                        break;
                }

                if (!providerResult) {
                    g_log.srtWarning("Could not initialize execution provider: %1. Using CPU.",
                                     providerResult.error().toString());
                } else if (ep != ExecutionProvider::CPU) {
                    g_log.srtInfo("Using execution provider %1 on device %2", static_cast<int>(ep),
                                  deviceIndex);
                }
            } else {
                g_log.srtInfo("The model requests CPU execution [%1]", modelPath.filename());
            }
            return Ort::Session{context.runtime().environment(),
                                std::filesystem::path::string_type(modelPath).c_str(),
                                sessionOptions};
        } catch (const Ort::Exception &error) {
            return srt::Error(srt::Error::InvalidFormat, error.what());
        }
    }

    SessionImage::SessionImage() : m_session(nullptr) {
    }

    SessionImage::~SessionImage() = default;

    srt::Expected<void> SessionImage::open(DriverContext &context,
                                           const std::filesystem::path &onnxPath, bool preferCpu) {
        const auto filename = onnxPath.filename();
        g_log.srtDebug("SessionImage [%1] - creating", filename);

        auto session = createOrtSession(context, onnxPath, preferCpu);
        if (!session) {
            g_log.srtCritical("SessionImage [%1] - create failed", filename);
            return session.takeError();
        }
        try {
            m_session = session.take();
            Ort::AllocatorWithDefaultOptions allocator;

            const auto inputCount = m_session.GetInputCount();
            m_inputNames.reserve(inputCount);
            for (size_t i = 0; i < inputCount; ++i) {
                m_inputNames.emplace_back(m_session.GetInputNameAllocated(i, allocator).get());
            }

            const auto outputCount = m_session.GetOutputCount();
            m_outputNames.reserve(outputCount);
            for (size_t i = 0; i < outputCount; ++i) {
                m_outputNames.emplace_back(m_session.GetOutputNameAllocated(i, allocator).get());
            }
        } catch (const Ort::Exception &error) {
            return srt::Error(srt::Error::InvalidFormat, error.what());
        }
        g_log.srtDebug("SessionImage [%1] - created successfully", filename);
        return {};
    }

    const stdc::vlarray_base<std::string> &SessionImage::inputNames() const noexcept {
        return m_inputNames;
    }

    const stdc::vlarray_base<std::string> &SessionImage::outputNames() const noexcept {
        return m_outputNames;
    }

    Ort::Session &SessionImage::session() noexcept {
        return m_session;
    }

}
