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
#include <dsinfer/Api/Inferences/Vocoder/2/VocoderApiL2.h>

#include <inferutil/Driver.h>
#include <inferutil/PluginCommon.h>

namespace srt::svs {

    namespace Co = Api::Common::L1;
    namespace Vo = Api::Vocoder::L1;
    namespace Vo_L2 = Api::Vocoder::L2;
    namespace Onnx = srt::driver::onnx;
    namespace DiffSinger = Api::DiffSinger::L1;

    static constexpr auto kLogPrefix = "[Vocoder]";

    static srt::LogCategory Log("diffsinger.vocoder");

    class VocoderInference::Impl {
    public:
        srt::core::NO<Vo_L2::VocoderResult> result;
        srt::core::NO<srt::driver::InferenceDriver> driver;
        srt::core::NO<srt::driver::InferenceSession> session;
        mutable std::shared_mutex mutex;
    };

    VocoderInference::VocoderInference(const srt::svs::InferenceSpec *spec)
        : Inference(spec), m_impl(std::make_unique<Impl>()) {
    }

    VocoderInference::~VocoderInference() = default;

    srt::core::Expected<void> VocoderInference::initialize(const srt::core::NO<srt::core::TaskInitArgs> &args) {
        auto &impl = *m_impl;
        // Currently, no args to process. But we still need to enforce callers to pass the correct
        // args type.
        if (auto res = ds::infer::inferutil::validateInitArgs(args, Vo::API_NAME, kLogPrefix); !res) {
            setState(Failed);
            Log.srtCritical("%1 initialize: %2", kLogPrefix, res.error().message());
            return res.takeError();
        }
        auto vocoderArgs = args.as<Vo::VocoderInitArgs>();
        if (!vocoderArgs) {
            setState(Failed);
            Log.srtCritical("%1 initialize: type mismatch, expected VocoderInitArgs", kLogPrefix);
            return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceInputInvalid,
                              "[Vocoder] type mismatch, expected VocoderInitArgs",
                              {}, "vocoder");
        }

        std::unique_lock<std::shared_mutex> lock(impl.mutex);

        // If there are existing result, they will be cleared.
        impl.result.reset();

        if (auto res = ds::infer::inferutil::getInferenceDriver(this); res) {
            impl.driver = res.take();
        } else {
            setState(Failed);
            Log.srtCritical("%1 initialize: failed to get inference driver: %2",
                            kLogPrefix, res.error().message());
            return res.takeError();
        }

        // Get vocoder config
        auto expConfig = ds::infer::inferutil::getTypedConfig<Vo::VocoderConfiguration>(spec(), Vo::API_CLASS, Vo::API_NAME, kLogPrefix);
        if (!expConfig) {
            setState(Failed);
            Log.srtCritical("%1 initialize: %2", kLogPrefix, expConfig.error().message());
            return expConfig.takeError();
        }
        const auto config = expConfig.take();

        // Open vocoder session
        if (auto exp = ds::infer::inferutil::openOnnxSession(
                impl.driver, config->model, false, "vocoder", kLogPrefix);
            exp) {
            impl.session = exp.take();
        } else {
            setState(Failed);
            Log.srtCritical("%1", exp.error().message());
            return exp.takeError();
        }

        // Initialize inference state. All sibling plugins (Duration/Pitch/
        // Variance/Acoustic) call setState(Idle) here; Vocoder must too so the
        // task state machine transitions correctly before start().
        setState(Idle);

        return srt::core::Expected<void>();
    }

    srt::core::Expected<srt::core::NO<srt::core::TaskResult>> VocoderInference::start(const srt::core::NO<srt::core::TaskStartInput> &input) {
        auto &impl = *m_impl;

        {
            std::shared_lock<std::shared_mutex> lock(impl.mutex);
            if (auto res = ds::infer::inferutil::checkDriverReady(impl.driver, kLogPrefix); !res) {
                setState(Failed);
                Log.srtCritical("%1", res.error().message());
                return res.takeError();
            }
        }

        setState(Running);

        // Get vocoder config
        auto expConfig = ds::infer::inferutil::getTypedConfig<Vo::VocoderConfiguration>(spec(), Vo::API_CLASS, Vo::API_NAME, kLogPrefix);
        if (!expConfig) {
            setState(Failed);
            Log.srtCritical("%1 start: %2", kLogPrefix, expConfig.error().message());
            return expConfig.takeError();
        }
        const auto config = expConfig.take();

        if (auto res = ds::infer::inferutil::validateStartInput(input, Vo::API_NAME, kLogPrefix); !res) {
            setState(Failed);
            Log.srtCritical("%1", res.error().message());
            return res.takeError();
        }

        const auto vocoderInput = input.as<Vo::VocoderStartInput>();
        if (!vocoderInput) {
            setState(Failed);
            Log.srtCritical("%1 start: type mismatch, expected VocoderStartInput", kLogPrefix);
            return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceInputInvalid,
                              "[Vocoder] type mismatch, expected VocoderStartInput",
                              {}, "vocoder");
        }
        // ...

        auto sessionInput = srt::core::NO<Onnx::SessionStartInput>::create();
        sessionInput->inputs["mel"] = vocoderInput->mel;
        sessionInput->inputs["f0"] = vocoderInput->f0;

        constexpr const char *outParamWaveform = "waveform";
        sessionInput->outputs.emplace(outParamWaveform);

        std::unique_lock<std::shared_mutex> lock(impl.mutex);
        if (!impl.session || !impl.session->isOpen()) {
            setState(Failed);
            Log.srtCritical("%1 start: session is not initialized or not open", kLogPrefix);
            return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceStartFailed,
                              "[Vocoder] session is not initialized",
                              {}, "vocoder");
        }

        srt::core::NO<srt::core::TaskResult> sessionTaskResult;
        auto sessionExp = impl.session->start(sessionInput);
        if (!sessionExp) {
            setState(Failed);
            Log.srtCritical("%1 start: session->start failed: %2",
                            kLogPrefix, sessionExp.error().message());
            return sessionExp.takeError();
        } else {
            sessionTaskResult = sessionExp.take();
        }

        auto vocoderResult = srt::core::NO<Vo_L2::VocoderResult>::create();

        // Get session results
        if (!sessionTaskResult) {
            setState(Failed);
            Log.srtCritical("%1 start: session result is nullptr", kLogPrefix);
            return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceOutputEmpty,
                              "[Vocoder] session result is nullptr",
                              {}, "vocoder");
        }
        if (sessionTaskResult->objectName() != Onnx::API_NAME) {
            setState(Failed);
            Log.srtCritical("%1 start: invalid result API name: %2",
                            kLogPrefix, sessionTaskResult->objectName());
            return srt::core::Error::inferenceError(srt::core::ErrorCode::InvalidArgument,
                              "[Vocoder] invalid result API name",
                              {}, "vocoder");
        }
        auto sessionResult = sessionTaskResult.as<Onnx::SessionResult>();
        if (!sessionResult) {
            setState(Failed);
            Log.srtCritical("%1 start: type mismatch, expected SessionResult", kLogPrefix);
            return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceRunFailed,
                              "[Vocoder] type mismatch, expected SessionResult",
                              {}, "vocoder");
        }
        if (auto it_waveform = sessionResult->outputs.find(outParamWaveform);
            it_waveform != sessionResult->outputs.end()) {
            const auto &waveformTensor = it_waveform->second;
            // BUG-20: validate waveform tensor is non-null and Float (float32 PCM)
            // before reinterpreting raw bytes as float. Without this, a malformed
            // ONNX output (wrong dtype / null tensor) would be silently memcpy'd
            // into audioData, producing garbage audio.
            if (!waveformTensor) {
                setState(Failed);
                Log.srtCritical("%1 start: waveform tensor is null", kLogPrefix);
                return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceOutputEmpty,
                                  "[Vocoder] waveform tensor is null",
                                  {}, "vocoder");
            }
            if (waveformTensor->dataType() != srt::core::ITensor::Float) {
                setState(Failed);
                Log.srtCritical("%1 start: waveform tensor dtype is not Float", kLogPrefix);
                return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceDataTypeMismatch,
                                  "[Vocoder] waveform tensor dtype is not Float",
                                  {}, "vocoder");
            }
            const auto size = waveformTensor->byteSize();
            // BUG-21: validate size is a multiple of sizeof(float) before
            // computing sampleCount. A non-multiple size would silently
            // truncate trailing bytes in the L1 path (and here would mis-size
            // the float vector).
            if (size % sizeof(float) != 0) {
                setState(Failed);
                Log.srtCritical(
                    "[Vocoder] start: waveform tensor byteSize is not a multiple of sizeof(float)");
                return srt::core::Error::inferenceError(
                    srt::core::ErrorCode::InferenceDataTypeMismatch,
                    "[Vocoder] waveform tensor byteSize is not a multiple of sizeof(float)",
                    {}, "vocoder");
            }
            const auto sampleCount = size / sizeof(float);
            vocoderResult->audioData.resize(sampleCount);
            if (auto waveformBuffer = waveformTensor->rawData()) {
                std::memcpy(vocoderResult->audioData.data(), waveformBuffer, size);
            } else {
                // BUG-PLUGIN-VOC-01: Do not silently return zero-filled audio
                // data when rawData() is null. Surface the failure explicitly
                // (ROBUST-05: no implicit error swallowing) and mirror the
                // error-handling pattern used by the sibling branches below.
                setState(Failed);
                Log.srtCritical("%1 start: waveform tensor rawData() is null", kLogPrefix);
                return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceOutputEmpty,
                                  "[Vocoder] waveform tensor rawData() is null",
                                  {}, "vocoder");
            }
        } else {
            setState(Failed);
            Log.srtCritical("%1 start: output 'waveform' not found in session result", kLogPrefix);
            return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceOutputEmpty,
                              "[Vocoder] output 'waveform' not found in session result",
                              {}, "vocoder");
        }
        // BUG-05: populate sampleRate/channels on the result so consumers no
        // longer need to hardcode 44100/mono. sampleRate comes from the L1
        // VocoderConfiguration (shared between L1/L2; only the result struct
        // was bumped). channels is 1 because the current vocoder outputs mono.
        vocoderResult->sampleRate = config->sampleRate;
        vocoderResult->channels = 1;
        impl.result = vocoderResult;

        setState(Idle);
        return vocoderResult;
    }

    srt::core::Expected<void> VocoderInference::startAsync(const srt::core::NO<srt::core::TaskStartInput> &input,
                                                     const StartAsyncCallback &callback) {
        // TODO:
        return srt::core::Error(srt::core::ErrorCode::NotImplemented, "not implemented");
    }

    bool VocoderInference::stop() {
        auto &impl = *m_impl;
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
        auto &impl = *m_impl;
        // Acquire the shared mutex like all sibling plugins (Duration/Pitch/
        // Variance/Acoustic) do, to avoid a data race with start() writing
        // impl.result concurrently.
        std::shared_lock<std::shared_mutex> lock(impl.mutex);
        return impl.result;
    }

}