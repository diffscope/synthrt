#include "AcousticInference.h"

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

namespace srt::svs {

    namespace Co = Api::Common::L1;
    namespace Ac = Api::Acoustic::L1;
    namespace Onnx = srt::driver::onnx;
    namespace DiffSinger = Api::DiffSinger::L1;

    static srt::LogCategory Log("diffsinger.acoustic");

    static inline srt::core::Expected<srt::core::NO<Ac::AcousticConfiguration>>
        getConfig(const srt::svs::InferenceSpec *spec) {

        const auto genericConfig = spec->configuration();
        if (!genericConfig) {
            return srt::core::Error(srt::core::Error::InvalidArgument,
                              "[Acoustic] configuration is nullptr");
        }
        if (!(genericConfig->className() == Ac::API_CLASS &&
              genericConfig->objectName() == Ac::API_NAME)) {
            return srt::core::Error(srt::core::Error::InvalidArgument,
                              "[Acoustic] invalid configuration class/name");
        }
        return genericConfig.as<Ac::AcousticConfiguration>();
    }

    class AcousticInference::Impl {
    public:
        srt::core::NO<Ac::AcousticResult> result;
        srt::core::NO<srt::driver::InferenceDriver> driver;
        srt::core::NO<srt::driver::InferenceSession> session;
        mutable std::shared_mutex mutex;
    };

    AcousticInference::AcousticInference(const srt::svs::InferenceSpec *spec)
        : Inference(spec), _impl(std::make_unique<Impl>()) {
    }

    AcousticInference::~AcousticInference() = default;

    srt::core::Expected<void> AcousticInference::initialize(const srt::core::NO<srt::core::TaskInitArgs> &args) {
        __stdc_impl_t;
        // Currently, no args to process. But we still need to enforce callers to pass the correct
        // args type.
        if (!args) {
            return srt::core::Error(srt::core::Error::InvalidArgument,
                              "[Acoustic] task init args is nullptr");
        }
        if (auto name = args->objectName(); name != Ac::API_NAME) {
            return srt::core::Error(
                srt::core::Error::InvalidArgument,
                stdc::formatN(R"([Acoustic] invalid task init args name: expected "%1", got "%2")",
                              Ac::API_NAME, name));
        }
        auto acousticArgs = args.as<Ac::AcousticInitArgs>();

        std::unique_lock<std::shared_mutex> lock(impl.mutex);

        // If there are existing result, they will be cleared.
        impl.result.reset();

        if (auto res = ds::infer::inferutil::getInferenceDriver(this); res) {
            impl.driver = res.take();
        } else {
            setState(Failed);
            Log.srtCritical("[Acoustic] initialize: failed to get inference driver: %1",
                            res.error().message());
            return res.takeError();
        }

        // Get acoustic config
        auto expConfig = getConfig(spec());
        if (!expConfig) {
            setState(Failed);
            Log.srtCritical("[Acoustic] initialize: %1", expConfig.error().message());
            return expConfig.takeError();
        }
        const auto config = expConfig.take();

        // Open acoustic session
        impl.session = impl.driver->createSession();
        auto sessionOpenArgs = srt::core::NO<Onnx::SessionOpenArgs>::create();
        sessionOpenArgs->useCpu = false;
        if (auto res = impl.session->open(config->model, sessionOpenArgs); !res) {
            setState(Failed);
            Log.srtCritical("[Acoustic] initialize: failed to open session for model %1",
                            stdc::path::to_utf8(config->model));
            return res;
        }

        // Initialize inference state
        setState(Idle);

        // return success
        return srt::core::Expected<void>();
    }

    srt::core::Expected<srt::core::NO<srt::core::TaskResult>>
        AcousticInference::start(const srt::core::NO<srt::core::TaskStartInput> &input) {

        __stdc_impl_t;

        {
            std::shared_lock<std::shared_mutex> lock(impl.mutex);
            if (!impl.driver) {
                setState(Failed);
                Log.srtCritical("[Acoustic] start: inference driver not initialized");
                return srt::core::Error(srt::core::Error::SessionError,
                                  "[Acoustic] inference driver not initialized");
            }
        }

        setState(Running);

        // Get acoustic config
        auto expConfig = getConfig(spec());
        if (!expConfig) {
            setState(Failed);
            Log.srtCritical("[Acoustic] start: %1", expConfig.error().message());
            return expConfig.takeError();
        }
        const auto config = expConfig.take();

        if (!input) {
            setState(Failed);
            Log.srtCritical("[Acoustic] start: input is nullptr");
            return srt::core::Error(srt::core::Error::InvalidArgument,
                              "[Acoustic] input is nullptr");
        }

        if (const auto &name = input->objectName(); name != Ac::API_NAME) {
            setState(Failed);
            Log.srtCritical("[Acoustic] start: invalid input name: expected %1, got %2",
                            Ac::API_NAME, name);
            return srt::core::Error(
                srt::core::Error::InvalidArgument,
                stdc::formatN(R"([Acoustic] invalid input name: expected "%1", got "%2")",
                              Ac::API_NAME, name));
        }

        const auto acousticInput = input.as<Ac::AcousticStartInput>();
        // ...

        auto sessionInput = srt::core::NO<Onnx::SessionStartInput>::create();

        double frameWidth = 1.0 * config->hopSize / config->sampleRate;

        // input param: tokens
        if (auto res =
                ds::infer::inferutil::preprocessPhonemeTokens(acousticInput->words, config->phonemes);
            res) {
            sessionInput->inputs["tokens"] = res.take();
        } else {
            setState(Failed);
            Log.srtCritical("[Acoustic] start: preprocessPhonemeTokens failed: %1",
                            res.error().message());
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
                Log.srtCritical("[Acoustic] start: preprocessPhonemeLanguages failed: %1",
                                res.error().message());
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
            Log.srtCritical("[Acoustic] start: preprocessPhonemeDurations failed: %1",
                            res.error().message());
            return res.takeError();
        }

        // input param: steps / speedup
        int64_t acceleration = acousticInput->steps;
        if (!config->useContinuousAcceleration) {
            acceleration = ds::infer::inferutil::getSpeedupFromSteps(acceleration);
        }
        {
            auto exp = srt::core::Tensor::createScalar<int64_t>(acceleration);
            if (!exp) {
                setState(Failed);
                Log.srtCritical("[Acoustic] start: failed to create steps/speedup tensor: %1",
                                exp.error().message());
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
                Log.srtCritical("[Acoustic] start: failed to create depth tensor: %1",
                                exp.error().message());
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
                Log.srtCritical("[Acoustic] start: failed to create depth tensor: %1",
                                exp.error().message());
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
        const Co::InputParameterInfo *pToneShiftParam = nullptr;

        for (const auto &param : acousticInput->parameters) {
            if (param.tag == Co::Tags::F0) {
                pF0Param = &param;
                continue;
            }

            if (param.tag == Co::Tags::Pitch) {
                pPitchParam = &param;
                continue;
            }

            if (param.tag == Co::Tags::ToneShift) {
                pToneShiftParam = &param;
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
                        Log.srtCritical("[Acoustic] start: failed to create gender fallback tensor: %1",
                                        exp.error().message());
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
                        Log.srtCritical("[Acoustic] start: failed to create velocity fallback tensor: %1",
                                        exp.error().message());
                        return exp.takeError();
                    }
                    sessionInput->inputs["velocity"] = exp.take();
                    satisfyVelocity = true;
                    continue;
                }
            }
            if (resampled.size() != targetLength) {
                setState(Failed);
                Log.srtCritical("[Acoustic] start: parameter %1 resample failed (size=%2, expected=%3)",
                                std::string(param.tag.name()), resampled.size(), targetLength);
                return srt::core::Error(srt::core::Error::SessionError,
                                "[Acoustic] parameter " +
                                std::string(param.tag.name()) +
                                " resample failed");
            }

            auto exp = ds::infer::inferutil::TensorHelper<float>::createFor1DArray(targetLength);
            if (!exp) {
                setState(Failed);
                Log.srtCritical("[Acoustic] start: failed to create tensor for param %1: %2",
                                std::string(param.tag.name()), exp.error().message());
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
                return srt::core::Error(srt::core::Error::SessionError,
                                "[Acoustic] parameter " +
                                std::string(param.tag.name()) +
                                " resample failed");
            }
            // Create f0 tensor for acoustic model
            auto expForAcoustic = ds::infer::inferutil::TensorHelper<float>::createFor1DArray(targetLength);
            if (!expForAcoustic) {
                Log.srtCritical("[Acoustic] start: failed to create f0 tensor: %1",
                                expForAcoustic.error().message());
                return expForAcoustic.takeError();
            }
            auto &acousticHelper = expForAcoustic.value();

            if (pToneShiftParam) {
                const auto &toneShift = *pToneShiftParam;
                if (!toneShift.values.empty()) {
                    auto toneShiftSamples = ds::infer::inferutil::resample(
                        toneShift.values, toneShift.interval, frameWidth, targetLength, false);
                    if (toneShiftSamples.size() != targetLength) {
                        return srt::core::Error(srt::core::Error::SessionError,
                                          "[Acoustic] parameter " + std::string(toneShift.tag.name()) +
                                              " resample failed");
                    }
                    if (convertToF0) {
                        for (size_t i = 0; i < targetLength; ++i) {
                            samples[i] += toneShiftSamples[i] / 100.0;
                        }
                    } else {
                        for (size_t i = 0; i < targetLength; ++i) {
                            samples[i] *= std::exp2(toneShiftSamples[i] / 1200.0);
                        }
                    }
                }
            }
            if (convertToF0) {
                // Convert midi note to hz
                for (const auto midi_note : std::as_const(samples)) {
                    constexpr double a4_freq_hz = 440.0;
                    constexpr double midi_a4_note = 69.0;
                    const auto f0Acoustic =
                        a4_freq_hz * std::exp2((midi_note - midi_a4_note) / 12.0);
                    // Buffer guaranteed not to overflow,
                    // given (resampled.size() == targetLength), which has been checked before
                    acousticHelper.writeUnchecked(static_cast<float>(f0Acoustic));
                }
            } else {
                for (const auto sample : std::as_const(samples)) {
                    // Buffer guaranteed not to overflow,
                    // given (resampled.size() == targetLength), which has been checked before
                    acousticHelper.writeUnchecked(static_cast<float>(sample));
                }
            }
            f0TensorForVocoder = acousticHelper.take();
            sessionInput->inputs["f0"] = f0TensorForVocoder; // ref count +1
            return srt::core::Expected<void>();
        };

        if (pF0Param) {
            // Has f0 parameter
            if (auto exp = processF0Param(*pF0Param, false); !exp) {
                setState(Failed);
                Log.srtCritical("[Acoustic] start: processF0Param(f0) failed: %1",
                                exp.error().message());
                return exp.takeError();
            }
        } else if (pPitchParam) {
            // Has pitch parameter
            if (auto exp = processF0Param(*pPitchParam, true); !exp) {
                setState(Failed);
                Log.srtCritical("[Acoustic] start: processF0Param(pitch) failed: %1",
                                exp.error().message());
                return exp.takeError();
            }
        } else {
            // No pitch or f0 found
            setState(Failed);
            Log.srtCritical("[Acoustic] start: parameter f0 or pitch missing");
            return srt::core::Error(srt::core::Error::SessionError,
                              "[Acoustic] parameter f0 or pitch missing");
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
            return srt::core::Error(srt::core::Error::SessionError, std::move(msg));
        }

        // Speaker embedding
        if (config->useSpeakerEmbedding) {
            if (acousticInput->speakers.empty()) {
                setState(Failed);
                Log.srtCritical("[Acoustic] start: no speakers found in input");
                return srt::core::Error(srt::core::Error::SessionError,
                                  "[Acoustic] no speakers found in input");
            }

            auto exp = ds::infer::inferutil::preprocessSpeakerEmbeddingFrames(
                acousticInput->speakers, config->speakers, config->hiddenSize, frameWidth,
                targetLength);
            if (exp) {
                sessionInput->inputs["spk_embed"] = exp.take();
            } else {
                setState(Failed);
                Log.srtCritical("[Acoustic] start: preprocessSpeakerEmbeddingFrames failed: %1",
                                exp.error().message());
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
            Log.srtCritical("[Acoustic] start: session is not initialized or not open");
            return srt::core::Error(srt::core::Error::SessionError,
                              "[Acoustic] session is not initialized");
        }

        srt::core::NO<srt::core::TaskResult> sessionTaskResult;
        auto sessionExp = impl.session->start(sessionInput);
        if (!sessionExp) {
            setState(Failed);
            Log.srtCritical("[Acoustic] start: session->start failed: %1",
                            sessionExp.error().message());
            return sessionExp.takeError();
        } else {
            sessionTaskResult = sessionExp.take();
        }

        auto acousticResult = srt::core::NO<Ac::AcousticResult>::create();

        // Get session results
        if (!sessionTaskResult) {
            setState(Failed);
            Log.srtCritical("[Acoustic] start: session result is nullptr");
            return srt::core::Error(srt::core::Error::SessionError,
                              "[Acoustic] session result is nullptr");
        }
        if (sessionTaskResult->objectName() != Onnx::API_NAME) {
            setState(Failed);
            Log.srtCritical("[Acoustic] start: invalid result API name: %1",
                            sessionTaskResult->objectName());
            return srt::core::Error(srt::core::Error::InvalidArgument,
                              "[Acoustic] invalid result API name");
        }
        auto sessionResult = sessionTaskResult.as<Onnx::SessionResult>();
        if (auto it_mel = sessionResult->outputs.find(outParamMel);
            it_mel != sessionResult->outputs.end()) {
            acousticResult->mel = it_mel->second;
        } else {
            setState(Failed);
            Log.srtCritical("[Acoustic] start: output 'mel' not found in session result");
            return srt::core::Error(srt::core::Error::SessionError,
                              "[Acoustic] output 'mel' not found in session result");
        }
        acousticResult->f0 = f0TensorForVocoder;
        impl.result = acousticResult;

        setState(Idle);
        return acousticResult;
    }

    srt::core::Expected<void> AcousticInference::startAsync(const srt::core::NO<srt::core::TaskStartInput> &input,
                                                      const StartAsyncCallback &callback) {
        // TODO:
        return srt::core::Error(srt::core::Error::NotImplemented);
    }

    bool AcousticInference::stop() {
        __stdc_impl_t;
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
        __stdc_impl_t;
        std::shared_lock<std::shared_mutex> lock(impl.mutex);
        return impl.result;
    }

}