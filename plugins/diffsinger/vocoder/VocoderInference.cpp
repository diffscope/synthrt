#include "VocoderInference.h"

#include <cstring>
#include <mutex>
#include <shared_mutex>
#include <utility>

#include <stdcorelib/pimpl.h>
#include <stdcorelib/str.h>
#include <stdcorelib/path.h>

#include <synthrt/Core/Support/Logging.h>

#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>
#include <synthrt/Driver/onnx/OnnxDriverApi.h>
#include <dsinfer/Api/Singers/DiffSinger/1/DiffSingerApiL1.h>
#include <dsinfer/Api/Inferences/Vocoder/1/VocoderApiL1.h>

#include <inferutil/Driver.h>

namespace srt::svs {

    namespace Co = Api::Common::L1;
    namespace Vo = Api::Vocoder::L1;
    namespace Onnx = srt::driver::onnx;
    namespace DiffSinger = Api::DiffSinger::L1;

    static srt::LogCategory Log("diffsinger.vocoder");

    static inline srt::core::Expected<srt::core::NO<Vo::VocoderConfiguration>>
        getConfig(const srt::svs::InferenceSpec *spec) {

        const auto genericConfig = spec->configuration();
        if (!genericConfig) {
            return srt::core::Error(srt::core::Error::InvalidArgument,
                              "[Vocoder] configuration is nullptr");
        }
        if (!(genericConfig->className() == Vo::API_CLASS &&
              genericConfig->objectName() == Vo::API_NAME)) {
            return srt::core::Error(srt::core::Error::InvalidArgument,
                              "[Vocoder] invalid configuration class/name");
        }
        return genericConfig.as<Vo::VocoderConfiguration>();
    }

    class VocoderInference::Impl {
    public:
        srt::core::NO<Vo::VocoderResult> result;
        srt::core::NO<srt::driver::InferenceDriver> driver;
        srt::core::NO<srt::driver::InferenceSession> session;
        mutable std::shared_mutex mutex;
    };

    VocoderInference::VocoderInference(const srt::svs::InferenceSpec *spec)
        : Inference(spec), _impl(std::make_unique<Impl>()) {
    }

    VocoderInference::~VocoderInference() = default;

    srt::core::Expected<void> VocoderInference::initialize(const srt::core::NO<srt::core::TaskInitArgs> &args) {
        __stdc_impl_t;
        // Currently, no args to process. But we still need to enforce callers to pass the correct
        // args type.
        if (!args) {
            return srt::core::Error(srt::core::Error::InvalidArgument,
                              "[Vocoder] task init args is nullptr");
        }
        if (auto name = args->objectName(); name != Vo::API_NAME) {
            return srt::core::Error(
                srt::core::Error::InvalidArgument,
                stdc::formatN(R"([Vocoder] invalid task init args name: expected "%1", got "%2")",
                              Vo::API_NAME, name));
        }
        auto vocoderArgs = args.as<Vo::VocoderInitArgs>();

        std::unique_lock<std::shared_mutex> lock(impl.mutex);

        // If there are existing result, they will be cleared.
        impl.result.reset();

        if (auto res = ds::infer::inferutil::getInferenceDriver(this); res) {
            impl.driver = res.take();
        } else {
            setState(Failed);
            Log.srtCritical("[Vocoder] initialize: failed to get inference driver: %1",
                            res.error().message());
            return res.takeError();
        }

        // Get vocoder config
        auto expConfig = getConfig(spec());
        if (!expConfig) {
            setState(Failed);
            Log.srtCritical("[Vocoder] initialize: %1", expConfig.error().message());
            return expConfig.takeError();
        }
        const auto config = expConfig.take();

        // Open vocoder session
        impl.session = impl.driver->createSession();
        auto sessionOpenArgs = srt::core::NO<Onnx::SessionOpenArgs>::create();
        sessionOpenArgs->useCpu = false;
        if (auto res = impl.session->open(config->model, sessionOpenArgs); !res) {
            setState(Failed);
            Log.srtCritical("[Vocoder] initialize: failed to open session for model %1",
                            stdc::path::to_utf8(config->model));
            return res;
        }

        // Initialize inference state. All sibling plugins (Duration/Pitch/
        // Variance/Acoustic) call setState(Idle) here; Vocoder must too so the
        // task state machine transitions correctly before start().
        setState(Idle);

        return srt::core::Expected<void>();
    }

    srt::core::Expected<srt::core::NO<srt::core::TaskResult>> VocoderInference::start(const srt::core::NO<srt::core::TaskStartInput> &input) {
        __stdc_impl_t;

        {
            std::shared_lock<std::shared_mutex> lock(impl.mutex);
            if (!impl.driver) {
                setState(Failed);
                Log.srtCritical("[Vocoder] start: inference driver not initialized");
                return srt::core::Error(srt::core::Error::SessionError,
                                  "[Vocoder] inference driver not initialized");
            }
        }

        setState(Running);

        // Get vocoder config
        auto expConfig = getConfig(spec());
        if (!expConfig) {
            setState(Failed);
            Log.srtCritical("[Vocoder] start: %1", expConfig.error().message());
            return expConfig.takeError();
        }
        const auto config = expConfig.take();

        if (!input) {
            setState(Failed);
            Log.srtCritical("[Vocoder] start: input is nullptr");
            return srt::core::Error(srt::core::Error::InvalidArgument,
                              "[Vocoder] input is nullptr");
        }

        if (const auto &name = input->objectName(); name != Vo::API_NAME) {
            setState(Failed);
            Log.srtCritical("[Vocoder] start: invalid input name: expected %1, got %2",
                            Vo::API_NAME, name);
            return srt::core::Error(
                srt::core::Error::InvalidArgument,
                stdc::formatN(R"([Vocoder] invalid input name: expected "%1", got "%2")",
                              Vo::API_NAME, name));
        }

        const auto vocoderInput = input.as<Vo::VocoderStartInput>();
        // ...

        auto sessionInput = srt::core::NO<Onnx::SessionStartInput>::create();
        sessionInput->inputs["mel"] = vocoderInput->mel;
        sessionInput->inputs["f0"] = vocoderInput->f0;

        constexpr const char *outParamWaveform = "waveform";
        sessionInput->outputs.emplace(outParamWaveform);

        std::unique_lock<std::shared_mutex> lock(impl.mutex);
        if (!impl.session || !impl.session->isOpen()) {
            setState(Failed);
            Log.srtCritical("[Vocoder] start: session is not initialized or not open");
            return srt::core::Error(srt::core::Error::SessionError,
                              "[Vocoder] session is not initialized");
        }

        srt::core::NO<srt::core::TaskResult> sessionTaskResult;
        auto sessionExp = impl.session->start(sessionInput);
        if (!sessionExp) {
            setState(Failed);
            Log.srtCritical("[Vocoder] start: session->start failed: %1",
                            sessionExp.error().message());
            return sessionExp.takeError();
        } else {
            sessionTaskResult = sessionExp.take();
        }

        auto vocoderResult = srt::core::NO<Vo::VocoderResult>::create();

        // Get session results
        if (!sessionTaskResult) {
            setState(Failed);
            Log.srtCritical("[Vocoder] start: session result is nullptr");
            return srt::core::Error(srt::core::Error::SessionError,
                              "[Vocoder] session result is nullptr");
        }
        if (sessionTaskResult->objectName() != Onnx::API_NAME) {
            setState(Failed);
            Log.srtCritical("[Vocoder] start: invalid result API name: %1",
                            sessionTaskResult->objectName());
            return srt::core::Error(srt::core::Error::InvalidArgument,
                              "[Vocoder] invalid result API name");
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
            Log.srtCritical("[Vocoder] start: output 'waveform' not found in session result");
            return srt::core::Error(srt::core::Error::SessionError,
                              "[Vocoder] output 'waveform' not found in session result");
        }
        impl.result = vocoderResult;

        setState(Idle);
        return vocoderResult;
    }

    srt::core::Expected<void> VocoderInference::startAsync(const srt::core::NO<srt::core::TaskStartInput> &input,
                                                     const StartAsyncCallback &callback) {
        // TODO:
        return srt::core::Error(srt::core::Error::NotImplemented);
    }

    bool VocoderInference::stop() {
        __stdc_impl_t;
        // Guard against stop() being called before initialize() has created
        // the session (or after initialize() failed early). Sibling plugins
        // (Duration/Pitch/Variance) check `if (session)` before dereferencing;
        // Vocoder must do the same to avoid a null pointer dereference.
        if (!impl.session) {
            return false;
        }
        if (!impl.session->isOpen()) {
            return false;
        }
        if (!impl.session->stop()) {
            return false;
        }
        setState(Terminated);
        return true;
    }

    srt::core::NO<srt::core::TaskResult> VocoderInference::result() const {
        __stdc_impl_t;
        // Acquire the shared mutex like all sibling plugins (Duration/Pitch/
        // Variance/Acoustic) do, to avoid a data race with start() writing
        // impl.result concurrently.
        std::shared_lock<std::shared_mutex> lock(impl.mutex);
        return impl.result;
    }

}