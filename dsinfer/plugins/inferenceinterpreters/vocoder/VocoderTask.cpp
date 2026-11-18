#include "VocoderTask.h"

#include <cstring>
#include <mutex>
#include <shared_mutex>
#include <tuple>
#include <utility>

#include <stdcorelib/str.h>

#include <dsinfer/Support/ErrorCode.h>
#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>
#include <dsinfer/Api/Drivers/Onnx/OnnxDriverApi.h>
#include <dsinfer/Api/Inferences/Vocoder/1/VocoderApiL1.h>
#include <dsinfer/Api/Inferences/Vocoder/1/VocoderOnnxApiL1.h>

#include <inferutil/Driver.h>

#include "VocoderInference.h"

namespace ds {

    namespace Co = Api::Common::L1;
    namespace Vo = Api::Vocoder::L1;
    namespace Onnx = Api::Onnx;

    static inline srt::Expected<const Vo::Onnx::Configuration *>
        getConfig(const srt::InferenceSpec &spec) {

        const auto genericConfig = spec.configuration();
        if (!genericConfig) {
            return srt::Error(srt::Error::InvalidArgument, "vocoder configuration is nullptr");
        }
        if (genericConfig->triplet() != Vo::Onnx::Triplet) {
            return srt::Error(srt::Error::InvalidArgument, "invalid vocoder configuration");
        }
        return static_cast<const Vo::Onnx::Configuration *>(genericConfig);
    }

    VocoderTask::VocoderTask(VocoderInference &inference) : m_inference(&inference) {
    }

    VocoderTask::~VocoderTask() {
        (void) stop();
        (void) waitForFinished();
    }

    srt::Expected<void> VocoderTask::initialize(const Vo::InitArgs &args) {
        return initialize(static_cast<const srt::TaskInitArgs &>(args));
    }

    srt::Expected<std::unique_ptr<Vo::Result>> VocoderTask::start(const Vo::StartInput &input) {
        auto result = ITask::start(input);
        if (!result) {
            return result.takeError();
        }
        return std::unique_ptr<Vo::Result>(static_cast<Vo::Result *>(result.take().release()));
    }

    srt::Expected<void> VocoderTask::startAsync(std::shared_ptr<const Vo::StartInput> input,
                                                Vo::Executive::AsyncCallback callback) {
        if (!callback) {
            return srt::Error(srt::Error::InvalidArgument,
                              "vocoder asynchronous callback must not be empty");
        }
        auto genericInput = std::static_pointer_cast<const srt::TaskStartInput>(std::move(input));
        return ITask::startAsync(
            std::move(genericInput),
            [callback = std::move(callback)](
                srt::Expected<std::unique_ptr<srt::TaskResult>> result) mutable {
                if (!result) {
                    callback(result.takeError());
                    return;
                }
                callback(std::unique_ptr<Vo::Result>(
                    static_cast<Vo::Result *>(result.take().release())));
            });
    }

    srt::Expected<void> VocoderTask::initialize(const srt::TaskInitArgs &args) {
        if (args.type() != Vo::Contract.interfaceId() || args.version() != Vo::Contract.level()) {
            return srt::Error(
                srt::Error::InvalidArgument,
                stdc::formatN(
                    R"(invalid vocoder initialization payload: expected "%1" level %2, got "%3" level %4)",
                    Vo::Contract.interfaceId(), Vo::Contract.level(), args.type(), args.version()));
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
        VocoderTask::execute(const srt::TaskStartInput &input) {

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


        if (input.type() != Vo::Contract.interfaceId() || input.version() != Vo::Contract.level()) {
            ITask::setState(ITask::Failed);
            return srt::Error(
                srt::Error::InvalidArgument,
                stdc::formatN(
                    R"(invalid vocoder start payload: expected "%1" level %2, got "%3" level %4)",
                    Vo::Contract.interfaceId(), Vo::Contract.level(), input.type(),
                    input.version()));
        }

        const auto &vocoderInput = *input.as<Vo::StartInput>();

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

        auto vocoderResult = std::make_unique<Vo::Result>();

        // Get session results
        if (!sessionTaskResult) {
            ITask::setState(ITask::Failed);
            return srt::Error(ds::ErrorCode::SessionFailed, "vocoder session result is nullptr");
        }
        if (sessionTaskResult->type() != Onnx::Name ||
            sessionTaskResult->version() != Onnx::Version) {
            ITask::setState(ITask::Failed);
            return srt::Error(srt::Error::InvalidArgument, "invalid result API name");
        }
        auto sessionResult = sessionTaskResult->as<Onnx::SessionResult>();
        if (auto waveformIt = sessionResult->outputs.find(outParamWaveform);
            waveformIt != sessionResult->outputs.end() && waveformIt->second) {
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
        ITask::setState(ITask::Succeeded);
        return std::unique_ptr<srt::TaskResult>(std::move(vocoderResult));
    }

    srt::Expected<void> VocoderTask::validateInput(const srt::TaskStartInput &input) const {
        if (input.type() != Vo::Contract.interfaceId() || input.version() != Vo::Contract.level()) {
            return srt::Error(srt::Error::InvalidArgument, "invalid vocoder start payload");
        }
        return {};
    }

    srt::Expected<void> VocoderTask::quit() {
        srt::Error stopError;
        for (auto session : {m_session.get()}) {
            if (session && session->state() == Running) {
                if (auto result = session->stop(); !result && stopError.ok()) {
                    stopError = result.takeError();
                }
            }
        }
        if (!stopError.ok()) {
            return stopError;
        }
        return {};
    }

    srt::Expected<void> VocoderTask::wait() {
        srt::Error waitError;
        for (auto session : {m_session.get()}) {
            if (session) {
                if (auto result = session->waitForFinished(); !result && waitError.ok()) {
                    waitError = result.takeError();
                }
            }
        }
        if (!waitError.ok()) {
            return waitError;
        }
        return {};
    }

    VocoderTask::State VocoderTask::classifyOutcome(
        const srt::Expected<std::unique_ptr<srt::TaskResult>> &outcome) noexcept {
        if (!outcome && stopRequested()) {
            return Canceled;
        }
        return outcome ? Succeeded : Failed;
    }

}
