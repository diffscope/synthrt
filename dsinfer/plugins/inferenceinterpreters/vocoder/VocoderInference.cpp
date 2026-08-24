#include "VocoderInference.h"

#include <cstring>
#include <mutex>
#include <shared_mutex>
#include <utility>

#include <stdcorelib/pimpl.h>
#include <stdcorelib/str.h>

#include <dsinfer/Support/ErrorCode.h>
#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>
#include <dsinfer/Api/Drivers/Onnx/OnnxDriverApi.h>
#include <dsinfer/Api/Inferences/Vocoder/1/VocoderApiL1.h>

#include <inferutil/Driver.h>

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

    class VocoderInference::Impl {
    public:
        InferenceDriver *driver = nullptr;
        std::unique_ptr<InferenceSession> session;
        mutable std::shared_mutex mutex;
    };

    VocoderInference::VocoderInference(srt::InferenceSpec &spec)
        : Inference(spec), _impl(std::make_unique<Impl>()) {
    }

    VocoderInference::~VocoderInference() = default;

    srt::Expected<void> VocoderInference::initialize(const srt::TaskInitArgs &args) {
        stdc_impl_t;
        // Currently, no args to process. But we still need to enforce callers to pass the correct
        // args type.
        if (auto name = args.type(); name != Vo::API_INTERFACE) {
            return srt::Error(
                srt::Error::InvalidArgument,
                stdc::formatN(R"(invalid vocoder task init args name: expected "%1", got "%2")",
                              Vo::API_INTERFACE, name));
        }
        const auto &vocoderArgs = *args.as<Vo::VocoderInitArgs>();

        std::unique_lock<std::shared_mutex> lock(impl.mutex);

        if (auto res = inferutil::getInferenceDriver(this); res) {
            impl.driver = res.take();
        } else {
            ITask::setState(ITask::Failed);
            return res.takeError();
        }

        // Get vocoder config
        auto expConfig = getConfig(spec());
        if (!expConfig) {
            ITask::setState(ITask::Failed);
            return expConfig.takeError();
        }
        const auto config = expConfig.take();

        // Open vocoder session
        impl.session = impl.driver->createSession();
        Onnx::SessionOpenArgs sessionOpenArgs;
        sessionOpenArgs.useCpu = false;
        if (auto res = impl.session->open(config->model, sessionOpenArgs); !res) {
            ITask::setState(ITask::Failed);
            return res;
        }

        return srt::Expected<void>();
    }

    srt::Expected<std::unique_ptr<srt::TaskResult>>
        VocoderInference::start(const srt::TaskStartInput &input) {
        stdc_impl_t;

        {
            std::shared_lock<std::shared_mutex> lock(impl.mutex);
            if (!impl.driver) {
                ITask::setState(ITask::Failed);
                return srt::Error(ds::ErrorCode::NotInitialized,
                                  "inference driver not initialized");
            }
        }

        ITask::setState(ITask::Running);

        // Get vocoder config
        auto expConfig = getConfig(spec());
        if (!expConfig) {
            ITask::setState(ITask::Failed);
            return expConfig.takeError();
        }
        const auto config = expConfig.take();


        if (const auto &name = input.type(); name != Vo::API_INTERFACE) {
            ITask::setState(ITask::Failed);
            return srt::Error(
                srt::Error::InvalidArgument,
                stdc::formatN(R"(invalid acoustic task init args name: expected "%1", got "%2")",
                              Vo::API_INTERFACE, name));
        }

        const auto &vocoderInput = *input.as<Vo::VocoderStartInput>();
        // ...

        auto sessionInput = std::make_shared<Onnx::SessionStartInput>();
        sessionInput->inputs["mel"] = vocoderInput.mel;
        sessionInput->inputs["f0"] = vocoderInput.f0;

        constexpr const char *outParamWaveform = "waveform";
        sessionInput->outputs.emplace(outParamWaveform);

        std::unique_lock<std::shared_mutex> lock(impl.mutex);
        if (!impl.session || !impl.session->isOpen()) {
            ITask::setState(ITask::Failed);
            return srt::Error(ds::ErrorCode::NotInitialized, "vocoder session is not initialized");
        }

        std::unique_ptr<srt::TaskResult> sessionTaskResult;
        auto sessionExp = impl.session->start(*sessionInput);
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
        if (auto it_waveform = sessionResult->outputs.find(outParamWaveform);
            it_waveform != sessionResult->outputs.end()) {
            const auto &waveformTensor = it_waveform->second;
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

    srt::Expected<void> VocoderInference::startAsync(std::shared_ptr<const srt::TaskStartInput>,
                                                     AsyncCallback) {
        // TODO:
        return srt::Error(srt::Error::NotImplemented);
    }

    srt::Expected<void> VocoderInference::stop() {
        stdc_impl_t;
        for (auto *session : {impl.session.get()}) {
            if (session) {
                if (auto result = session->stop(); !result) {
                    return result;
                }
            }
        }
        ITask::setState(ITask::Canceled);
        return {};
    }

    srt::Expected<void> VocoderInference::waitForFinished() {
        stdc_impl_t;
        for (auto *session : {impl.session.get()}) {
            if (session) {
                if (auto result = session->waitForFinished(); !result) {
                    return result;
                }
            }
        }
        return {};
    }

}
