#include "AcousticInference.h"

#include <mutex>
#include <shared_mutex>
#include <utility>

#include <stdcorelib/str.h>
#include <stdcorelib/path.h>

#include <dsinfer/Support/ErrorCode.h>
#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>
#include <dsinfer/Api/Inferences/Acoustic/1/AcousticApiL1.h>
#include <dsinfer/Api/Drivers/Onnx/OnnxDriverApi.h>
#include <dsinfer/Inference/InferenceDriver.h>
#include <dsinfer/Inference/InferenceSession.h>
#include <dsinfer/Core/ParamTag.h>
#include <dsinfer/Core/Tensor.h>

#include <inferutil/Driver.h>
#include <inferutil/Algorithm.h>
#include <inferutil/TensorHelper.h>
#include <inferutil/InputWord.h>
#include <inferutil/SpeakerEmbedding.h>
#include <inferutil/Speedup.h>

namespace ds {

    namespace Co = Api::Common::L1;
    namespace Ac = Api::Acoustic::L1;
    namespace Onnx = Api::Onnx;

    static inline srt::Expected<const Ac::AcousticConfiguration *>
        getConfig(const srt::InferenceSpec &spec) {

        const auto genericConfig = spec.configuration();
        if (!genericConfig) {
            return srt::Error(srt::Error::InvalidArgument, "acoustic configuration is nullptr");
        }
        if (genericConfig->interface() != Ac::API_INTERFACE ||
            genericConfig->variant() != Ac::API_VARIANT ||
            genericConfig->level() != Ac::API_LEVEL) {
            return srt::Error(srt::Error::InvalidArgument, "invalid acoustic configuration");
        }
        return static_cast<const Ac::AcousticConfiguration *>(genericConfig);
    }

    AcousticInference::AcousticInference(srt::InferenceSpec &spec) : InferenceExecInstance(spec) {
    }

    AcousticInference::~AcousticInference() = default;

    srt::Expected<void> AcousticInference::initialize(const srt::TaskInitArgs &args) {
        if (args.type() != Ac::API_INTERFACE || args.version() != Ac::API_LEVEL) {
            return srt::Error(
                srt::Error::InvalidArgument,
                stdc::formatN(
                    R"(invalid acoustic initialization payload: expected "%1" level %2, got "%3" level %4)",
                    Ac::API_INTERFACE, Ac::API_LEVEL, args.type(), args.version()));
        }

        std::unique_lock<std::shared_mutex> lock(m_mutex);

        if (auto res = inferutil::getInferenceDriver(this); res) {
            m_driver = res.take();
        } else {
            ITask::setState(ITask::Failed);
            return res.takeError();
        }

        auto expConfig = getConfig(spec());
        if (!expConfig) {
            ITask::setState(ITask::Failed);
            return expConfig.takeError();
        }
        const auto config = expConfig.take();

        // Open acoustic session
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
        AcousticInference::start(const srt::TaskStartInput &input) {

        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            if (!m_driver) {
                ITask::setState(ITask::Failed);
                return srt::Error(ds::ErrorCode::NotInitialized,
                                  "inference driver not initialized");
            }
        }

        ITask::setState(ITask::Running);

        auto expConfig = getConfig(spec());
        if (!expConfig) {
            ITask::setState(ITask::Failed);
            return expConfig.takeError();
        }
        const auto config = expConfig.take();


        if (input.type() != Ac::API_INTERFACE || input.version() != Ac::API_LEVEL) {
            ITask::setState(ITask::Failed);
            return srt::Error(
                srt::Error::InvalidArgument,
                stdc::formatN(
                    R"(invalid acoustic start payload: expected "%1" level %2, got "%3" level %4)",
                    Ac::API_INTERFACE, Ac::API_LEVEL, input.type(), input.version()));
        }

        const auto &acousticInput = *input.as<Ac::AcousticStartInput>();

        auto sessionInput = std::make_shared<Onnx::SessionStartInput>();

        double frameWidth = 1.0 * config->hopSize / config->sampleRate;

        // Build the phoneme token input.
        if (auto res = inferutil::preprocessPhonemeTokens(acousticInput.words, config->phonemes);
            res) {
            sessionInput->inputs["tokens"] = res.take();
        } else {
            ITask::setState(ITask::Failed);
            return res.takeError().withContext(R"(failed to build the "tokens" input)");
        }

        // Build the optional language input.
        if (config->useLanguageId) {
            if (auto res =
                    inferutil::preprocessPhonemeLanguages(acousticInput.words, config->languages);
                res) {
                sessionInput->inputs["languages"] = res.take();
            } else {
                ITask::setState(ITask::Failed);
                return res.takeError().withContext(R"(failed to build the "languages" input)");
            }
        }

        // Build the phoneme duration input.
        int64_t targetLength;

        if (auto res = inferutil::preprocessPhonemeDurations(acousticInput.words, frameWidth,
                                                             &targetLength);
            res) {
            sessionInput->inputs["durations"] = res.take();
        } else {
            ITask::setState(ITask::Failed);
            return res.takeError().withContext(R"(failed to build the "durations" input)");
        }

        // Select the model's acceleration representation.
        int64_t acceleration = acousticInput.steps;
        if (config->useContinuousAcceleration) {
            // Here \a steps reaches the model unchanged, and is also used as a divisor when
            // computing \a depth below. \c getSpeedupFromSteps() has a fallback for non-positive
            // input, but this branch has none, so reject it instead of dividing by zero.
            if (acceleration <= 0) {
                ITask::setState(ITask::Failed);
                return srt::Error(srt::Error::InvalidArgument,
                                  "acoustic input: steps must be a positive integer");
            }
        } else {
            // Always >= 1, see \c getSpeedupFromSteps().
            acceleration = inferutil::getSpeedupFromSteps(acceleration);
        }
        {
            const char *inputName = config->useContinuousAcceleration ? "steps" : "speedup";
            auto exp = Tensor::createScalar<int64_t>(acceleration);
            if (!exp) {
                ITask::setState(ITask::Failed);
                return exp.takeError().withContext(
                    stdc::formatN(R"(failed to build the "%1" input)", inputName));
            }
            if (config->useContinuousAcceleration) {
                sessionInput->inputs["steps"] = exp.take();
            } else {
                sessionInput->inputs["speedup"] = exp.take();
            }
        }

        // Build the diffusion depth input.
        if (config->useVariableDepth) {
            auto exp = Tensor::createScalar<float>(acousticInput.depth);
            if (!exp) {
                ITask::setState(ITask::Failed);
                return exp.takeError().withContext(R"(failed to build the "depth" input)");
            }
            sessionInput->inputs["depth"] = exp.take();
        } else {
            int64_t intDepth = std::llround(acousticInput.depth * 1000);
            intDepth = (std::min) (intDepth, static_cast<int64_t>(config->maxDepth));
            // make sure depth can be divided by speedup, with \a acceleration guaranteed >= 1 above
            intDepth = intDepth / acceleration * acceleration;

            auto exp = Tensor::createScalar<int64_t>(intDepth);
            if (!exp) {
                ITask::setState(ITask::Failed);
                return exp.takeError().withContext(R"(failed to build the "depth" input)");
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

        std::shared_ptr<ITensor> f0TensorForVocoder;

        const Co::InputParameterInfo *pPitchParam = nullptr;
        const Co::InputParameterInfo *pF0Param = nullptr;
        const Co::InputParameterInfo *pToneShiftParam = nullptr;

        for (const auto &param : acousticInput.parameters) {
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
            auto resampled =
                inferutil::resample(param.values, param.interval, frameWidth, targetLength, true);
            if (resampled.empty()) {
                // These parameters are optional
                if (param.tag == Co::Tags::Gender) {
                    // Fill gender with 0
                    auto exp =
                        Tensor::createFilled<float>(std::vector<int64_t>{1, targetLength}, 0.0f);
                    if (!exp) {
                        ITask::setState(ITask::Failed);
                        return exp.takeError().withContext(R"(failed to build the "gender" input)");
                    }
                    sessionInput->inputs["gender"] = exp.take();
                    satisfyGender = true;
                    continue;
                }
                if (param.tag == Co::Tags::Velocity) {
                    // Fill velocity with 0
                    auto exp =
                        Tensor::createFilled<float>(std::vector<int64_t>{1, targetLength}, 1.0f);
                    if (!exp) {
                        ITask::setState(ITask::Failed);
                        return exp.takeError().withContext(
                            R"(failed to build the "velocity" input)");
                    }
                    sessionInput->inputs["velocity"] = exp.take();
                    satisfyVelocity = true;
                    continue;
                }
            }
            if (resampled.size() != targetLength) {
                ITask::setState(ITask::Failed);
                return srt::Error(ds::ErrorCode::ProcessingFailed,
                                  "parameter " + std::string(param.tag.name()) +
                                      " resample failed");
            }

            auto exp = inferutil::TensorHelper<float>::createFor1DArray(targetLength);
            if (!exp) {
                ITask::setState(ITask::Failed);
                return exp.takeError().withContext(
                    stdc::formatN(R"(failed to build the "%1" input)", param.tag.name()));
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
                                        bool convertToF0) -> srt::Expected<void> {
            // Resample parameter
            auto samples =
                inferutil::resample(param.values, param.interval, frameWidth, targetLength, true);
            if (samples.size() != targetLength) {
                return srt::Error(ds::ErrorCode::ProcessingFailed,
                                  "parameter " + std::string(param.tag.name()) +
                                      " resample failed");
            }
            // Create f0 tensor for acoustic model
            auto expForAcoustic = inferutil::TensorHelper<float>::createFor1DArray(targetLength);
            if (!expForAcoustic) {
                return expForAcoustic.takeError().withContext(
                    R"(failed to build the "f0" input for the acoustic model)");
            }
            auto &acousticHelper = expForAcoustic.value();

            if (pToneShiftParam) {
                const auto &toneShift = *pToneShiftParam;
                if (!toneShift.values.empty()) {
                    auto toneShiftSamples = inferutil::resample(
                        toneShift.values, toneShift.interval, frameWidth, targetLength, false);
                    if (toneShiftSamples.size() != targetLength) {
                        return srt::Error(ds::ErrorCode::ProcessingFailed,
                                          "parameter " + std::string(toneShift.tag.name()) +
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
                for (const auto midiNote : std::as_const(samples)) {
                    constexpr double a4Frequency = 440.0;
                    constexpr double midiA4Note = 69.0;
                    const auto f0Acoustic = a4Frequency * std::exp2((midiNote - midiA4Note) / 12.0);
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
            return srt::Expected<void>();
        };

        if (pF0Param) {
            // Has f0 parameter
            if (auto exp = processF0Param(*pF0Param, false); !exp) {
                ITask::setState(ITask::Failed);
                return exp.takeError().withContext(R"(failed to process the "f0" parameter)");
            }
        } else if (pPitchParam) {
            // Has pitch parameter
            if (auto exp = processF0Param(*pPitchParam, true); !exp) {
                ITask::setState(ITask::Failed);
                return exp.takeError().withContext(R"(failed to process the "pitch" parameter)");
            }
        } else {
            // No pitch or f0 found
            ITask::setState(ITask::Failed);
            return srt::Error(ds::ErrorCode::InvalidInput, "parameter f0 or pitch missing");
        }

        // Some parameter requirements are not satisfied
        if (!satisfyEnergy || !satisfyBreathiness || !satisfyVoicing || !satisfyTension) {
            ITask::setState(ITask::Failed);
            std::string msg = "some required parameters missing:";
            if (!satisfyEnergy)
                msg += R"( "energy")";
            if (!satisfyBreathiness)
                msg += R"( "breathiness")";
            if (!satisfyVoicing)
                msg += R"( "voicing")";
            if (!satisfyTension)
                msg += R"( "tension")";
            return srt::Error(ds::ErrorCode::InvalidInput, std::move(msg));
        }

        // Speaker embedding
        if (config->useSpeakerEmbedding) {
            if (acousticInput.speakers.empty()) {
                ITask::setState(ITask::Failed);
                return srt::Error(ds::ErrorCode::InvalidInput,
                                  "no speakers found in acoustic input");
            }

            auto exp = inferutil::preprocessSpeakerEmbeddingFrames(
                acousticInput.speakers, config->speakers, config->hiddenSize, frameWidth,
                targetLength);
            if (exp) {
                sessionInput->inputs["spk_embed"] = exp.take();
            } else {
                ITask::setState(ITask::Failed);
                return exp.takeError().withContext(R"(failed to build the "spk_embed" input)");
            }
        }

        constexpr const char *outParamMel = "mel";
        sessionInput->outputs.emplace(outParamMel);

        std::unique_lock<std::shared_mutex> lock(m_mutex);
        if (!m_session || !m_session->isOpen()) {
            ITask::setState(ITask::Failed);
            return srt::Error(ds::ErrorCode::NotInitialized, "acoustic session is not initialized");
        }

        std::unique_ptr<srt::TaskResult> sessionTaskResult;
        auto sessionExp = m_session->start(*sessionInput);
        if (!sessionExp) {
            ITask::setState(ITask::Failed);
            return sessionExp.takeError();
        } else {
            sessionTaskResult = sessionExp.take();
        }

        auto acousticResult = std::make_unique<Ac::AcousticResult>();

        // Get session results
        if (!sessionTaskResult) {
            ITask::setState(ITask::Failed);
            return srt::Error(ds::ErrorCode::SessionFailed, "acoustic session result is nullptr");
        }
        if (sessionTaskResult->type() != Onnx::API_NAME) {
            ITask::setState(ITask::Failed);
            return srt::Error(srt::Error::InvalidArgument, "invalid result API name");
        }
        auto sessionResult = sessionTaskResult->as<Onnx::SessionResult>();
        if (auto melIt = sessionResult->outputs.find(outParamMel);
            melIt != sessionResult->outputs.end()) {
            acousticResult->mel = melIt->second;
        } else {
            ITask::setState(ITask::Failed);
            return srt::Error(ds::ErrorCode::SessionFailed, "invalid result output");
        }
        acousticResult->f0 = f0TensorForVocoder;
        ITask::setState(ITask::Idle);
        return std::unique_ptr<srt::TaskResult>(std::move(acousticResult));
    }

    srt::Expected<void> AcousticInference::startAsync(std::shared_ptr<const srt::TaskStartInput>,
                                                      AsyncCallback) {
        return srt::Error(srt::Error::NotImplemented);
    }

    srt::Expected<void> AcousticInference::stop() {
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

    srt::Expected<void> AcousticInference::waitForFinished() {
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
