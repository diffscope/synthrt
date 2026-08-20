#include "AcousticInference.h"

#include <cmath>
#include <mutex>
#include <shared_mutex>
#include <utility>

#include <stdcorelib/pimpl.h>
#include <stdcorelib/str.h>
#include <stdcorelib/path.h>

#include <synthrt/Core/Support/Logging.h>

#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>
#include <dsinfer/Api/Inferences/Acoustic/1/AcousticApiL1.h>
#include <synthrt/Driver/onnx/OnnxDriverApi.h>
#include <dsinfer/Api/Singers/DiffSinger/1/DiffSingerApiL1.h>
#include <synthrt/Driver/InferenceDriver.h>
#include <synthrt/Driver/InferenceSession.h>
#include <dsinfer/Core/ParamTag.h>
#include <synthrt/Core/Tensor/ITensor.h>
#include <synthrt/Core/Tensor/Tensor.h>

#include <inferutil/Driver.h>
#include <inferutil/Algorithm.h>
#include <inferutil/TensorHelper.h>
#include <inferutil/InputWord.h>
#include <inferutil/SpeakerEmbedding.h>
#include <inferutil/Speedup.h>
#include <inferutil/PluginCommon.h>

namespace srt::svs {

    namespace Co = Api::Common::L1;
    namespace Ac = Api::Acoustic::L1;
    namespace Onnx = srt::driver::onnx;
    namespace DiffSinger = Api::DiffSinger::L1;

    static constexpr auto kLogPrefix = "[Acoustic]";

    static srt::LogCategory Log("diffsinger.acoustic");

    class AcousticInference::Impl {
    public:
        srt::core::NO<Ac::AcousticResult> result;
        srt::core::NO<srt::driver::InferenceDriver> driver;
        srt::core::NO<srt::driver::InferenceSession> session;
        mutable std::shared_mutex mutex;
    };

    AcousticInference::AcousticInference(const srt::svs::InferenceSpec *spec)
        : Inference(spec), m_impl(std::make_unique<Impl>()) {
    }

    AcousticInference::~AcousticInference() = default;

    srt::core::Expected<void> AcousticInference::initialize(const srt::core::NO<srt::core::TaskInitArgs> &args) {
        auto &impl = *m_impl;
        // Currently, no args to process. But we still need to enforce callers to pass the correct
        // args type.
        if (auto res = ds::infer::inferutil::validateInitArgs(args, Ac::API_NAME, kLogPrefix); !res) {
            setState(Failed);
            Log.srtCritical("%1 initialize: %2", kLogPrefix, res.error().message());
            return res.takeError();
        }
        auto acousticArgs = args.as<Ac::AcousticInitArgs>();
        if (!acousticArgs) {
            setState(Failed);
            Log.srtCritical("%1 initialize: type mismatch, expected AcousticInitArgs", kLogPrefix);
            return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceInputInvalid,
                              "[Acoustic] type mismatch, expected AcousticInitArgs",
                              {}, "acoustic");
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

        // Get acoustic config
        auto expConfig = ds::infer::inferutil::getTypedConfig<Ac::AcousticConfiguration>(spec(), Ac::API_CLASS, Ac::API_NAME, kLogPrefix);
        if (!expConfig) {
            setState(Failed);
            Log.srtCritical("%1 initialize: %2", kLogPrefix, expConfig.error().message());
            return expConfig.takeError();
        }
        const auto config = expConfig.take();

        // Open acoustic session
        if (auto exp = ds::infer::inferutil::openOnnxSession(
                impl.driver, config->model, false, "acoustic", kLogPrefix);
            exp) {
            impl.session = exp.take();
        } else {
            setState(Failed);
            Log.srtCritical("%1", exp.error().message());
            return exp.takeError();
        }

        // Initialize inference state
        setState(Idle);

        // return success
        return srt::core::Expected<void>();
    }

    srt::core::Expected<srt::core::NO<srt::core::TaskResult>>
        AcousticInference::start(const srt::core::NO<srt::core::TaskStartInput> &input) {

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

        // Get acoustic config
        auto expConfig = ds::infer::inferutil::getTypedConfig<Ac::AcousticConfiguration>(spec(), Ac::API_CLASS, Ac::API_NAME, kLogPrefix);
        if (!expConfig) {
            setState(Failed);
            Log.srtCritical("%1 start: %2", kLogPrefix, expConfig.error().message());
            return expConfig.takeError();
        }
        const auto config = expConfig.take();

        if (auto res = ds::infer::inferutil::validateStartInput(input, Ac::API_NAME, kLogPrefix); !res) {
            setState(Failed);
            Log.srtCritical("%1", res.error().message());
            return res.takeError();
        }

        const auto acousticInput = input.as<Ac::AcousticStartInput>();
        if (!acousticInput) {
            setState(Failed);
            Log.srtCritical("%1 start: type mismatch, expected AcousticStartInput", kLogPrefix);
            return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceInputInvalid,
                              "[Acoustic] type mismatch, expected AcousticStartInput",
                              {}, "acoustic");
        }
        // ...

        auto sessionInput = srt::core::NO<Onnx::SessionStartInput>::create();

        double frameWidth = 1.0 * config->hopSize / config->sampleRate;
        // BF-35: Validate frameWidth before use. Duration/Pitch/Variance all
        // check this; Acoustic was missing the guard, risking division by zero
        // in preprocessPhonemeDurations and silent skips in resample.
        if (auto res = ds::infer::inferutil::validateFrameWidth(frameWidth, kLogPrefix); !res) {
            setState(Failed);
            Log.srtCritical("%1", res.error().message());
            return res.takeError();
        }

        // input param: tokens
        if (auto res =
                ds::infer::inferutil::preprocessPhonemeTokens(acousticInput->words, config->phonemes);
            res) {
            sessionInput->inputs["tokens"] = res.take();
        } else {
            setState(Failed);
            Log.srtCritical("%1 start: preprocessPhonemeTokens failed: %2",
                                kLogPrefix, res.error().message());
            return res.takeError();
        }

        // input param: languages
        if (config->useLanguageId) {
            if (auto res = ds::infer::inferutil::preprocessPhonemeLanguages(acousticInput->words,
                                                                         config->languages);
                res) {
                sessionInput->inputs["languages"] = res.take();
            } else {
                setState(Failed);
                Log.srtCritical("%1 start: preprocessPhonemeLanguages failed: %2",
                                kLogPrefix, res.error().message());
                return res.takeError();
            }
        }

        // input param: durations
        int64_t targetLength;

        if (auto res = ds::infer::inferutil::preprocessPhonemeDurations(acousticInput->words,
                                                                     frameWidth, &targetLength);
            res) {
            sessionInput->inputs["durations"] = res.take();
        } else {
            setState(Failed);
            Log.srtCritical("%1 start: preprocessPhonemeDurations failed: %2",
                                kLogPrefix, res.error().message());
            return res.takeError();
        }

        // input param: steps / speedup
        int64_t acceleration = acousticInput->steps;
        if (!config->useContinuousAcceleration) {
            acceleration = ds::infer::inferutil::getSpeedupFromSteps(acceleration);
            // BF-28: Guard against speedup being 0 to prevent division by zero
            // in depth calculation and downstream model inference.
            if (acceleration < 1) {
                acceleration = 1;
            }
        }
        {
            auto exp = srt::core::Tensor::createScalar<int64_t>(acceleration);
            if (!exp) {
                setState(Failed);
                Log.srtCritical("%1 start: failed to create steps/speedup tensor: %2",
                                kLogPrefix, exp.error().message());
                return exp.takeError();
            }
            if (config->useContinuousAcceleration) {
                sessionInput->inputs["steps"] = exp.take();
            } else {
                sessionInput->inputs["speedup"] = exp.take();
            }
        }

        // input param: depth
        if (config->useVariableDepth) {
            auto exp = srt::core::Tensor::createScalar<float>(acousticInput->depth);
            if (!exp) {
                setState(Failed);
                Log.srtCritical("%1 start: failed to create depth tensor: %2",
                                kLogPrefix, exp.error().message());
                return exp.takeError();
            }
            sessionInput->inputs["depth"] = exp.take();
        } else {
            int64_t intDepth = std::llround(acousticInput->depth * 1000);
            intDepth = (std::min)(intDepth, static_cast<int64_t>(config->maxDepth));
            // make sure depth can be divided by speedup
            if (acceleration > 0) {
                intDepth = intDepth / acceleration * acceleration;
            }

            auto exp = srt::core::Tensor::createScalar<int64_t>(intDepth);
            if (!exp) {
                setState(Failed);
                Log.srtCritical("%1 start: failed to create depth tensor: %2",
                                kLogPrefix, exp.error().message());
                return exp.takeError();
            }
            sessionInput->inputs["depth"] = exp.take();
        }

        // We define some requirements according to config.
        //
        // If the config supports a parameter, the flag is set to false, and
        // when the input contains such valid parameter, the flag is then set to true.
        //
        // If the config does NOT support a parameter, the flag is automatically set to true.
        // No need to check such input.
        const auto hasParam = [&](const ParamTag &tag) -> bool {
            return config->parameters.find(tag) != config->parameters.end();
        };

        bool satisfyGender = !hasParam(Co::Tags::Gender);
        bool satisfyVelocity = !hasParam(Co::Tags::Velocity);

        bool satisfyEnergy = !hasParam(Co::Tags::Energy);
        bool satisfyBreathiness = !hasParam(Co::Tags::Breathiness);
        bool satisfyVoicing = !hasParam(Co::Tags::Voicing);
        bool satisfyTension = !hasParam(Co::Tags::Tension);
        bool satisfyMouthOpening = !hasParam(Co::Tags::MouthOpening);

        srt::core::NO<srt::core::ITensor> f0TensorForVocoder;

        const Co::InputParameterInfo *pPitchParam = nullptr;
        const Co::InputParameterInfo *pF0Param = nullptr;

        for (const auto &param : acousticInput->parameters) {
            if (param.tag == Co::Tags::F0) {
                pF0Param = &param;
                continue;
            }

            if (param.tag == Co::Tags::Pitch) {
                pPitchParam = &param;
                continue;
            }

            // Resample the parameters to target time step,
            // and resize to target frame length (fill with last value)
            auto resampled = ds::infer::inferutil::resample(param.values, param.interval, frameWidth,
                                                         targetLength, true);
            if (resampled.empty()) {
                // These parameters are optional
                if (param.tag == Co::Tags::Gender) {
                    // Fill gender with 0
                    auto exp =
                        srt::core::Tensor::createFilled<float>(std::vector<int64_t>{1, targetLength}, 0.0f);
                    if (!exp) {
                        setState(Failed);
                        Log.srtCritical("%1 start: failed to create gender fallback tensor: %2",
                                        kLogPrefix, exp.error().message());
                        return exp.takeError();
                    }
                    sessionInput->inputs["gender"] = exp.take();
                    satisfyGender = true;
                    continue;
                }
                if (param.tag == Co::Tags::Velocity) {
                    // Fill velocity with 0
                    auto exp =
                        srt::core::Tensor::createFilled<float>(std::vector<int64_t>{1, targetLength}, 1.0f);
                    if (!exp) {
                        setState(Failed);
                        Log.srtCritical("%1 start: failed to create velocity fallback tensor: %2",
                                        kLogPrefix, exp.error().message());
                        return exp.takeError();
                    }
                    sessionInput->inputs["velocity"] = exp.take();
                    satisfyVelocity = true;
                    continue;
                }
            }
            if (resampled.size() != targetLength) {
                setState(Failed);
                Log.srtCritical("%1 start: parameter %2 resample failed (size=%3, expected=%4)",
                                kLogPrefix, std::string(param.tag.name()), resampled.size(), targetLength);
                return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceInputInvalid,
                                "[Acoustic] parameter " +
                                std::string(param.tag.name()) +
                                " resample failed", {}, "acoustic");
            }

            auto exp = ds::infer::inferutil::TensorHelper<float>::createFor1DArray(targetLength);
            if (!exp) {
                setState(Failed);
                Log.srtCritical("%1 start: failed to create tensor for param %2: %3",
                                kLogPrefix, std::string(param.tag.name()), exp.error().message());
                return exp.takeError();
            }
            auto &helper = exp.value();

            // for other parameters, simply fill them in.
            for (const auto item : std::as_const(resampled)) {
                helper.writeUnchecked(static_cast<float>(item));
            }
            if (!satisfyGender && param.tag == Co::Tags::Gender) {
                sessionInput->inputs["gender"] = helper.take();
                satisfyGender = true;
                continue;
            }
            if (!satisfyVelocity && param.tag == Co::Tags::Velocity) {
                sessionInput->inputs["velocity"] = helper.take();
                satisfyVelocity = true;
                continue;
            }
            if (!satisfyEnergy && param.tag == Co::Tags::Energy) {
                sessionInput->inputs["energy"] = helper.take();
                satisfyEnergy = true;
                continue;
            }
            if (!satisfyBreathiness && param.tag == Co::Tags::Breathiness) {
                sessionInput->inputs["breathiness"] = helper.take();
                satisfyBreathiness = true;
                continue;
            }
            if (!satisfyVoicing && param.tag == Co::Tags::Voicing) {
                sessionInput->inputs["voicing"] = helper.take();
                satisfyVoicing = true;
                continue;
            }
            if (!satisfyTension && param.tag == Co::Tags::Tension) {
                sessionInput->inputs["tension"] = helper.take();
                satisfyTension = true;
                continue;
            }
            if (!satisfyMouthOpening && param.tag == Co::Tags::MouthOpening) {
                sessionInput->inputs["mouth_opening"] = helper.take();
                satisfyMouthOpening = true;
                continue;
            }
        }

        // First check for f0.
        // If f0 missing, then check for pitch (midi pitch, will be converted to f0)
        const auto processF0Param = [&](const Co::InputParameterInfo &param,
                                        bool convertToF0) -> srt::core::Expected<void> {
            // Resample parameter
            auto samples =
                ds::infer::inferutil::resample(param.values, param.interval, frameWidth, targetLength, true);
            if (samples.size() != targetLength) {
                return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceInputInvalid,
                                "[Acoustic] parameter " +
                                std::string(param.tag.name()) +
                                " resample failed", {}, "acoustic");
            }

            // Convert midi note to hz
            const auto toHz = [](double note) -> float {
                constexpr double a4_freq_hz = 440.0;
                constexpr double midi_a4_note = 69.0;
                return static_cast<float>(a4_freq_hz * std::exp2((note - midi_a4_note) / 12.0));
            };
            const auto fillF0 = [&](ds::infer::inferutil::TensorHelper<float> &helper,
                                    const std::vector<double> &value) {
                if (convertToF0) {
                    for (const auto item : std::as_const(value)) {
                        // Buffer guaranteed not to overflow,
                        // given samples.size() == targetLength, checked above
                        helper.writeUnchecked(toHz(item));
                    }
                } else {
                    for (const auto item : std::as_const(value)) {
                        // Buffer guaranteed not to overflow,
                        // given samples.size() == targetLength, checked above
                        helper.writeUnchecked(static_cast<float>(item));
                    }
                }
            };

            // f0 tensor for the acoustic model (un-shifted, faithful)
            auto expForAcoustic = ds::infer::inferutil::TensorHelper<float>::createFor1DArray(targetLength);
            if (!expForAcoustic) {
                Log.srtCritical("%1 start: failed to create f0 tensor: %2",
                                kLogPrefix, expForAcoustic.error().message());
                return expForAcoustic.takeError();
            }
            auto &acousticHelper = expForAcoustic.value();
            fillF0(acousticHelper, samples);
            sessionInput->inputs["f0"] = acousticHelper.take(); // ref count +1

            // f0 tensor for the vocoder (un-shifted, faithful)
            auto expForVocoder = ds::infer::inferutil::TensorHelper<float>::createFor1DArray(targetLength);
            if (!expForVocoder) {
                Log.srtCritical("%1 start: failed to create vocoder f0 tensor: %2",
                                kLogPrefix, expForVocoder.error().message());
                return expForVocoder.takeError();
            }
            auto &vocoderHelper = expForVocoder.value();
            fillF0(vocoderHelper, samples);
            f0TensorForVocoder = vocoderHelper.take();
            return srt::core::Expected<void>();
        };

        if (pF0Param) {
            // Has f0 parameter
            if (auto exp = processF0Param(*pF0Param, false); !exp) {
                setState(Failed);
                Log.srtCritical("%1 start: processF0Param(f0) failed: %2",
                                kLogPrefix, exp.error().message());
                return exp.takeError();
            }
        } else if (pPitchParam) {
            // Has pitch parameter
            if (auto exp = processF0Param(*pPitchParam, true); !exp) {
                setState(Failed);
                Log.srtCritical("%1 start: processF0Param(pitch) failed: %2",
                                kLogPrefix, exp.error().message());
                return exp.takeError();
            }
        } else {
            // No pitch or f0 found
            setState(Failed);
            Log.srtCritical("%1 start: parameter f0 or pitch missing", kLogPrefix);
            return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceInputInvalid,
                              "[Acoustic] parameter f0 or pitch missing",
                              {}, "acoustic");
        }

        // Some parameter requirements are not satisfied
        if (!satisfyEnergy || !satisfyBreathiness || !satisfyVoicing || !satisfyTension) {
            setState(Failed);
            std::string msg = "[Acoustic] some required parameters missing:";
            if (!satisfyEnergy)
                msg += R"( "energy")";
            if (!satisfyBreathiness)
                msg += R"( "breathiness")";
            if (!satisfyVoicing)
                msg += R"( "voicing")";
            if (!satisfyTension)
                msg += R"( "tension")";
            Log.srtCritical("%1", msg);
            return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceInputInvalid, std::move(msg),
                              {}, "acoustic");
        }

        // Speaker embedding
        if (config->useSpeakerEmbedding) {
            if (acousticInput->speakers.empty()) {
                setState(Failed);
                Log.srtCritical("%1 start: no speakers found in input", kLogPrefix);
                return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceSpeakerNotFound,
                                  "[Acoustic] no speakers found in input",
                                  {}, "acoustic");
            }

            auto exp = ds::infer::inferutil::preprocessSpeakerEmbeddingFrames(
                acousticInput->speakers, config->speakers, config->hiddenSize, frameWidth,
                targetLength);
            if (exp) {
                sessionInput->inputs["spk_embed"] = exp.take();
            } else {
                setState(Failed);
                Log.srtCritical("%1 start: preprocessSpeakerEmbeddingFrames failed: %2",
                                kLogPrefix, exp.error().message());
                return exp.takeError();
            }
        } else {
            // Nothing to do: speaker embedding is not supported
        }

        constexpr const char *outParamMel = "mel";
        sessionInput->outputs.emplace(outParamMel);

        std::unique_lock<std::shared_mutex> lock(impl.mutex);
        if (!impl.session || !impl.session->isOpen()) {
            setState(Failed);
            Log.srtCritical("%1 start: session is not initialized or not open", kLogPrefix);
            return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceStartFailed,
                              "[Acoustic] session is not initialized",
                              {}, "acoustic");
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

        auto acousticResult = srt::core::NO<Ac::AcousticResult>::create();

        // Get session results
        if (!sessionTaskResult) {
            setState(Failed);
            Log.srtCritical("%1 start: session result is nullptr", kLogPrefix);
            return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceOutputEmpty,
                              "[Acoustic] session result is nullptr",
                              {}, "acoustic");
        }
        if (sessionTaskResult->objectName() != Onnx::API_NAME) {
            setState(Failed);
            Log.srtCritical("%1 start: invalid result API name: %2",
                            kLogPrefix, sessionTaskResult->objectName());
            return srt::core::Error::inferenceError(srt::core::ErrorCode::InvalidArgument,
                              "[Acoustic] invalid result API name",
                              {}, "acoustic");
        }
        auto sessionResult = sessionTaskResult.as<Onnx::SessionResult>();
        if (auto it_mel = sessionResult->outputs.find(outParamMel);
            it_mel != sessionResult->outputs.end()) {
            const auto &melTensor = it_mel->second;
            // BUG-19: validate mel tensor before handing it to the vocoder.
            // Without these checks, a null / wrong-dtype / empty mel tensor
            // would be forwarded to the vocoder, which then either crashes
            // (null deref) or produces garbage audio (wrong dtype reinterpreted
            // as float). Surface the failure explicitly (ROBUST-05).
            if (!melTensor) {
                setState(Failed);
                Log.srtCritical("%1 start: mel tensor is null", kLogPrefix);
                return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceOutputEmpty,
                                  "[Acoustic] mel tensor is null",
                                  {}, "acoustic");
            }
            if (melTensor->dataType() != srt::core::ITensor::Float) {
                setState(Failed);
                Log.srtCritical("%1 start: mel tensor dtype is not Float", kLogPrefix);
                return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceDataTypeMismatch,
                                  "[Acoustic] mel tensor dtype is not Float",
                                  {}, "acoustic");
            }
            if (melTensor->byteSize() == 0) {
                setState(Failed);
                Log.srtCritical("%1 start: mel tensor is empty", kLogPrefix);
                return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceOutputEmpty,
                                  "[Acoustic] mel tensor is empty",
                                  {}, "acoustic");
            }
            acousticResult->mel = melTensor;
        } else {
            setState(Failed);
            Log.srtCritical("%1 start: output 'mel' not found in session result", kLogPrefix);
            return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceOutputEmpty,
                              "[Acoustic] output 'mel' not found in session result",
                              {}, "acoustic");
        }
        acousticResult->f0 = f0TensorForVocoder;
        impl.result = acousticResult;

        setState(Idle);
        return acousticResult;
    }

    srt::core::Expected<void> AcousticInference::startAsync(const srt::core::NO<srt::core::TaskStartInput> &input,
                                                      const StartAsyncCallback &callback) {
        // TODO:
        return srt::core::Error(srt::core::ErrorCode::NotImplemented, "not implemented");
    }

    bool AcousticInference::stop() {
        auto &impl = *m_impl;
        // Guard against stop() being called before initialize() has created
        // the session (or after initialize() failed early). Duration/Pitch/
        // Variance all check `if (session)` before dereferencing; Acoustic
        // must do the same to avoid a null pointer dereference.
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

    srt::core::NO<srt::core::TaskResult> AcousticInference::result() const {
        auto &impl = *m_impl;
        std::shared_lock<std::shared_mutex> lock(impl.mutex);
        return impl.result;
    }

}