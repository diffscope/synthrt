#include "VocoderInference.h"

#include <cstring>
#include <mutex>
#include <shared_mutex>
#include <utility>

#include <stdcorelib/pimpl.h>
#include <stdcorelib/str.h>

#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>
#include <dsinfer/Api/Drivers/Onnx/OnnxDriverApi.h>
#include <dsinfer/Api/Singers/DiffSinger/1/DiffSingerApiL1.h>
#include <dsinfer/Api/Inferences/Vocoder/1/VocoderApiL1.h>

#include <inferutil/Driver.h>

namespace ds {

    namespace Co = Api::Common::L1;
    namespace Vo = Api::Vocoder::L1;
    namespace Onnx = Api::Onnx;
    namespace DiffSinger = Api::DiffSinger::L1;

    static inline srt::Expected<srt::NO<Vo::VocoderConfiguration>>
        getConfig(const srt::InferenceSpec *spec) {

        const auto genericConfig = spec->configuration();
        if (!genericConfig) {
            return srt::Error(srt::Error::InvalidArgument, "vocoder configuration is nullptr");
        }
        if (!(genericConfig->className() == Vo::API_CLASS &&
              genericConfig->objectName() == Vo::API_NAME)) {
            return srt::Error(srt::Error::InvalidArgument, "invalid vocoder configuration");
        }
        return genericConfig.as<Vo::VocoderConfiguration>();
    }

    class VocoderInference::Impl {
    public:
        srt::NO<Vo::VocoderResult> result;
        srt::NO<InferenceDriver> driver;
        srt::NO<InferenceSession> session;
        mutable std::shared_mutex mutex;
    };

    VocoderInference::VocoderInference(const srt::InferenceSpec *spec)
        : Inference(spec), _impl(std::make_unique<Impl>()) {
    }

    VocoderInference::~VocoderInference() = default;

    srt::Expected<void> VocoderInference::initialize(const srt::NO<srt::TaskInitArgs> &args) {
        __stdc_impl_t;
        // Currently, no args to process. But we still need to enforce callers to pass the correct
        // args type.
        if (!args) {
            return srt::Error(srt::Error::InvalidArgument, "vocoder task init args is nullptr");
        }
        if (auto name = args->objectName(); name != Vo::API_NAME) {
            return srt::Error(
                srt::Error::InvalidArgument,
                stdc::formatN(R"(invalid vocoder task init args name: expected "%1", got "%2")",
                              Vo::API_NAME, name));
        }
        auto vocoderArgs = args.as<Vo::VocoderInitArgs>();

        std::unique_lock<std::shared_mutex> lock(impl.mutex);

        // If there are existing result, they will be cleared.
        impl.result.reset();

        if (auto res = inferutil::getInferenceDriver(this); res) {
            impl.driver = res.take();
        } else {
            setState(Failed);
            return res.takeError();
        }

        // Get vocoder config
        auto expConfig = getConfig(spec());
        if (!expConfig) {
            setState(Failed);
            return expConfig.takeError();
        }
        const auto config = expConfig.take();

        // Open vocoder session
        impl.session = impl.driver->createSession();
        auto sessionOpenArgs = srt::NO<Onnx::SessionOpenArgs>::create();
        sessionOpenArgs->useCpu = false;
        if (auto res = impl.session->open(config->model, sessionOpenArgs); !res) {
            setState(Failed);
            return res;
        }

        return srt::Expected<void>();
    }

    srt::Expected<srt::NO<srt::TaskResult>> VocoderInference::start(const srt::NO<srt::TaskStartInput> &input) {
        __stdc_impl_t;

        {
            std::shared_lock<std::shared_mutex> lock(impl.mutex);
            if (!impl.driver) {
                setState(Failed);
                return srt::Error(srt::Error::SessionError, "inference driver not initialized");
            }
        }

        setState(Running);

        // Get vocoder config
        auto expConfig = getConfig(spec());
        if (!expConfig) {
            setState(Failed);
            return expConfig.takeError();
        }
        const auto config = expConfig.take();

        if (!input) {
            setState(Failed);
            return srt::Error(srt::Error::InvalidArgument, "vocoder input is nullptr");
        }

        if (const auto &name = input->objectName(); name != Vo::API_NAME) {
            setState(Failed);
            return srt::Error(
                srt::Error::InvalidArgument,
                stdc::formatN(R"(invalid acoustic task init args name: expected "%1", got "%2")",
                              Vo::API_NAME, name));
        }

        const auto vocoderInput = input.as<Vo::VocoderStartInput>();
        // ...

        auto sessionInput = srt::NO<Onnx::SessionStartInput>::create();
        sessionInput->inputs["mel"] = vocoderInput->mel;
        sessionInput->inputs["f0"] = vocoderInput->f0;

        constexpr const char *outParamWaveform = "waveform";
        sessionInput->outputs.emplace(outParamWaveform);

        std::unique_lock<std::shared_mutex> lock(impl.mutex);
        if (!impl.session || !impl.session->isOpen()) {
            setState(Failed);
            return srt::Error(srt::Error::SessionError, "vocoder session is not initialized");
        }

        srt::NO<srt::TaskResult> sessionTaskResult;
        auto sessionExp = impl.session->start(sessionInput);
        if (!sessionExp) {
            setState(Failed);
            return sessionExp.takeError();
        } else {
            sessionTaskResult = sessionExp.take();
        }

        auto vocoderResult = srt::NO<Vo::VocoderResult>::create();

        // Get session results
        if (!sessionTaskResult) {
            setState(Failed);
            return srt::Error(srt::Error::SessionError, "vocoder session result is nullptr");
        }
        if (sessionTaskResult->objectName() != Onnx::API_NAME) {
            setState(Failed);
            return srt::Error(srt::Error::InvalidArgument, "invalid result API name");
        }
        auto sessionResult = sessionTaskResult.as<Onnx::SessionResult>();
        if (auto it_waveform = sessionResult->outputs.find(outParamWaveform);
            it_waveform != sessionResult->outputs.end()) {
            const auto &waveformTensor = it_waveform->second;
            const auto size = waveformTensor->byteSize();
            vocoderResult->audioData.resize(size);
            if (auto waveformBuffer = waveformTensor->rawData()) {
                std::memcpy(vocoderResult->audioData.data(), waveformBuffer, size);
            }
        } else {
            setState(Failed);
            return srt::Error(srt::Error::SessionError, "invalid result output");
        }
        impl.result = vocoderResult;

        setState(Idle);
        return vocoderResult;
    }

    srt::Expected<void> VocoderInference::startAsync(const srt::NO<srt::TaskStartInput> &input,
                                                     const StartAsyncCallback &callback) {
        // TODO:
        return srt::Error(srt::Error::NotImplemented);
    }

    bool VocoderInference::stop() {
        __stdc_impl_t;
        if (!impl.session->isOpen()) {
            return false;
        }
        if (!impl.session->stop()) {
            return false;
        }
        setState(Terminated); // 设置状态
        return true;
    }

    srt::NO<srt::TaskResult> VocoderInference::result() const {
        __stdc_impl_t;
        return impl.result;
    }

}