#include "VocoderTask.h"

#include <cstring>
#include <mutex>
#include <shared_mutex>
#include <utility>

#include <stdcorelib/str.h>

#include <dsinfer/Support/ErrorCode.h>
#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>
#include <dsinfer/Api/Drivers/Onnx/OnnxDriverApi.h>
#include <dsinfer/Api/Inferences/Vocoder/1/VocoderApiL1.h>

#include <inferutil/Driver.h>

#include "VocoderInference.h"

namespace ds {

    namespace Co = Api::Common::L1;
    namespace Vo = Api::Vocoder::L1;
    namespace Onnx = Api::Onnx;

    static inline srt::Expected<const Vo::VocoderConfiguration *>
        getConfig(const srt::InferenceSpec &spec) {

        const auto genericConfig = spec.configuration();
        if (!genericConfig) {
            return srt::Error(srt::Error::InvalidArgument, "vocoder configuration is nullptr");
        }
        if (genericConfig->interface() != Vo::API_INTERFACE ||
            genericConfig->variant() != Vo::API_VARIANT ||
            genericConfig->level() != Vo::API_LEVEL) {
            return srt::Error(srt::Error::InvalidArgument, "invalid vocoder configuration");
        }
        return static_cast<const Vo::VocoderConfiguration *>(genericConfig);
    }

    VocoderTask::VocoderTask(VocoderInference &inference) : m_inference(&inference) {
    }

    VocoderTask::~VocoderTask() = default;

    srt::Expected<void> VocoderTask::initialize(const Vo::VocoderInitArgs &args) {
        return initialize(static_cast<const srt::TaskInitArgs &>(args));
    }

    srt::Expected<std::unique_ptr<Vo::VocoderResult>>
        VocoderTask::start(const Vo::VocoderStartInput &input) {
        auto result = start(static_cast<const srt::TaskStartInput &>(input));
        if (!result) {
            return result.takeError();
        }
        return std::unique_ptr<Vo::VocoderResult>(
            static_cast<Vo::VocoderResult *>(result.take().release()));
    }

    srt::Expected<void> VocoderTask::startAsync(std::shared_ptr<const Vo::VocoderStartInput> input,
                                                Vo::VocoderExecInstance::AsyncCallback callback) {
        auto genericInput = std::static_pointer_cast<const srt::TaskStartInput>(std::move(input));
        return startAsync(std::move(genericInput),
                          [callback = std::move(callback)](
                              srt::Expected<std::unique_ptr<srt::TaskResult>> result) mutable {
                              if (!result) {
                                  callback(result.takeError());
                                  return;
                              }
                              callback(std::unique_ptr<Vo::VocoderResult>(
                                  static_cast<Vo::VocoderResult *>(result.take().release())));
                          });
    }

    srt::Expected<void> VocoderTask::initialize(const srt::TaskInitArgs &args) {
        if (args.type() != Vo::API_INTERFACE || args.version() != Vo::API_LEVEL) {
            return srt::Error(
                srt::Error::InvalidArgument,
                stdc::formatN(
                    R"(invalid vocoder initialization payload: expected "%1" level %2, got "%3" level %4)",
                    Vo::API_INTERFACE, Vo::API_LEVEL, args.type(), args.version()));
        }

        std::unique_lock<std::shared_mutex> lock(m_mutex);

        if (auto res = inferutil::getInferenceDriver(m_inference); res) {
            m_driver = res.take();
        } else {
            ITask::setState(ITask::Failed);
            return res.takeError();
        }

        auto expConfig = getConfig(m_inference->spec());
        if (!expConfig) {
            ITask::setState(ITask::Failed);
            return expConfig.takeError();
        }
        const auto config = expConfig.take();

        // Open vocoder session
        m_session = m_driver->createSession();
        Onnx::SessionOpenArgs sessionOpenArgs;
        sessionOpenArgs.useCpu = false;
        if (auto res = m_session->open(config->model, sessionOpenArgs); !res) {
            ITask::setState(ITask::Failed);
            return res;
        }

        ITask::setState(ITask::Idle);
        return {};
    }

    srt::Expected<std::unique_ptr<srt::TaskResult>>
        VocoderTask::start(const srt::TaskStartInput &input) {

        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            if (!m_driver) {
                ITask::setState(ITask::Failed);
                return srt::Error(ds::ErrorCode::NotInitialized,
                                  "inference driver not initialized");
            }
        }

        ITask::setState(ITask::Running);

        auto expConfig = getConfig(m_inference->spec());
        if (!expConfig) {
            ITask::setState(ITask::Failed);
            return expConfig.takeError();
        }
        const auto config = expConfig.take();


        if (input.type() != Vo::API_INTERFACE || input.version() != Vo::API_LEVEL) {
            ITask::setState(ITask::Failed);
            return srt::Error(
                srt::Error::InvalidArgument,
                stdc::formatN(
                    R"(invalid vocoder start payload: expected "%1" level %2, got "%3" level %4)",
                    Vo::API_INTERFACE, Vo::API_LEVEL, input.type(), input.version()));
        }

        const auto &vocoderInput = *input.as<Vo::VocoderStartInput>();

        auto sessionInput = std::make_shared<Onnx::SessionStartInput>();
        sessionInput->inputs["mel"] = vocoderInput.mel;
        sessionInput->inputs["f0"] = vocoderInput.f0;

        constexpr const char *outParamWaveform = "waveform";
        sessionInput->outputs.emplace(outParamWaveform);

        std::unique_lock<std::shared_mutex> lock(m_mutex);
        if (!m_session || !m_session->isOpen()) {
            ITask::setState(ITask::Failed);
            return srt::Error(ds::ErrorCode::NotInitialized, "vocoder session is not initialized");
        }

        std::unique_ptr<srt::TaskResult> sessionTaskResult;
        auto sessionExp = m_session->start(*sessionInput);
        if (!sessionExp) {
            ITask::setState(ITask::Failed);
            return sessionExp.takeError();
        } else {
            sessionTaskResult = sessionExp.take();
        }

        auto vocoderResult = std::make_unique<Vo::VocoderResult>();

        // Get session results
        if (!sessionTaskResult) {
            ITask::setState(ITask::Failed);
            return srt::Error(ds::ErrorCode::SessionFailed, "vocoder session result is nullptr");
        }
        if (sessionTaskResult->type() != Onnx::API_NAME) {
            ITask::setState(ITask::Failed);
            return srt::Error(srt::Error::InvalidArgument, "invalid result API name");
        }
        auto sessionResult = sessionTaskResult->as<Onnx::SessionResult>();
        if (auto waveformIt = sessionResult->outputs.find(outParamWaveform);
            waveformIt != sessionResult->outputs.end()) {
            const auto &waveformTensor = waveformIt->second;
            const auto size = waveformTensor->byteSize();
            vocoderResult->audioData.resize(size);
            if (auto waveformBuffer = waveformTensor->rawData()) {
                std::memcpy(vocoderResult->audioData.data(), waveformBuffer, size);
            }
        } else {
            ITask::setState(ITask::Failed);
            return srt::Error(ds::ErrorCode::SessionFailed, "invalid result output");
        }
        ITask::setState(ITask::Idle);
        return std::unique_ptr<srt::TaskResult>(std::move(vocoderResult));
    }

    srt::Expected<void> VocoderTask::startAsync(std::shared_ptr<const srt::TaskStartInput>,
                                                AsyncCallback) {
        return srt::Error(srt::Error::NotImplemented);
    }

    srt::Expected<void> VocoderTask::stop() {
        for (auto *session : {m_session.get()}) {
            if (session) {
                if (auto result = session->stop(); !result) {
                    return result;
                }
            }
        }
        ITask::setState(ITask::Canceled);
        return {};
    }

    srt::Expected<void> VocoderTask::waitForFinished() {
        for (auto *session : {m_session.get()}) {
            if (session) {
                if (auto result = session->waitForFinished(); !result) {
                    return result;
                }
            }
        }
        return {};
    }

}
