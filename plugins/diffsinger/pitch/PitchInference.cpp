#include "PitchInference.h"

#include <cmath>
#include <mutex>
#include <numeric>
#include <shared_mutex>
#include <utility>

#include <stdcorelib/pimpl.h>
#include <stdcorelib/str.h>
#include <stdcorelib/path.h>

#include <synthrt/Core/Support/Logging.h>

#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>
#include <dsinfer/Api/Inferences/Pitch/1/PitchApiL1.h>
#include <synthrt/Driver/onnx/OnnxDriverApi.h>
#include <dsinfer/Api/Singers/DiffSinger/1/DiffSingerApiL1.h>
#include <synthrt/Driver/InferenceDriver.h>
#include <synthrt/Driver/InferenceSession.h>
#include <synthrt/Core/Tensor/ITensor.h>
#include <synthrt/Core/Tensor/Tensor.h>

#include <inferutil/Driver.h>
#include <inferutil/Algorithm.h>
#include <inferutil/InputWord.h>
#include <inferutil/LinguisticEncoder.h>
#include <inferutil/SpeakerEmbedding.h>
#include <inferutil/Speedup.h>

namespace srt::svs {

    namespace Co = Api::Common::L1;
    namespace Pit = Api::Pitch::L1;
    namespace Onnx = srt::driver::onnx;
    namespace DiffSinger = Api::DiffSinger::L1;

    static srt::LogCategory Log("diffsinger.pitch");

    static inline srt::core::Expected<srt::core::NO<Pit::PitchConfiguration>>
        getConfig(const srt::svs::InferenceSpec *spec) {

        const auto genericConfig = spec->configuration();
        if (!genericConfig) {
            return srt::core::Error(srt::core::Error::InvalidArgument,
                              "[Pitch] configuration is nullptr");
        }
        if (!(genericConfig->className() == Pit::API_CLASS &&
              genericConfig->objectName() == Pit::API_NAME)) {
            return srt::core::Error(srt::core::Error::InvalidArgument,
                              "[Pitch] invalid configuration");
        }
        return genericConfig.as<Pit::PitchConfiguration>();
    }

    class PitchInference::Impl {
    public:
        srt::core::NO<Pit::PitchResult> result;
        srt::core::NO<srt::driver::InferenceDriver> driver;
        srt::core::NO<srt::driver::InferenceSession> encoderSession;
        srt::core::NO<srt::driver::InferenceSession> predictorSession;
        mutable std::shared_mutex mutex;
    };

    PitchInference::PitchInference(const srt::svs::InferenceSpec *spec)
        : Inference(spec), _impl(std::make_unique<Impl>()) {
    }

    PitchInference::~PitchInference() = default;

    srt::core::Expected<void> PitchInference::initialize(const srt::core::NO<srt::core::TaskInitArgs> &args) {
        __stdc_impl_t;
        // Currently, no args to process. But we still need to enforce callers to pass the correct
        // args type.
        if (!args) {
            return srt::core::Error(srt::core::Error::InvalidArgument,
                              "[Pitch] task init args is nullptr");
        }
        if (auto name = args->objectName(); name != Pit::API_NAME) {
            return srt::core::Error(
                srt::core::Error::InvalidArgument,
                stdc::formatN(R"([Pitch] invalid task init args name: expected "%1", got "%2")",
                              Pit::API_NAME, name));
        }
        auto pitchArgs = args.as<Pit::PitchInitArgs>();

        std::unique_lock<std::shared_mutex> lock(impl.mutex);

        // If there are existing result, they will be cleared.
        impl.result.reset();

        if (auto res = ds::infer::inferutil::getInferenceDriver(this); res) {
            impl.driver = res.take();
        } else {
            setState(Failed);
            Log.srtCritical("[Pitch] initialize: failed to get inference driver: %1",
                            res.error().message());
            return res.takeError();
        }

        // Get pitch config
        auto expConfig = getConfig(spec());
        if (!expConfig) {
            setState(Failed);
            Log.srtCritical("[Pitch] initialize: %1", expConfig.error().message());
            return expConfig.takeError();
        }
        const auto config = expConfig.take();

        // Open pitch session (encoder)
        impl.encoderSession = impl.driver->createSession();
        auto encoderOpenArgs = srt::core::NO<Onnx::SessionOpenArgs>::create();
        encoderOpenArgs->useCpu = false;
        if (auto res = impl.encoderSession->open(config->encoder, encoderOpenArgs); !res) {
            setState(Failed);
            Log.srtCritical("[Pitch] initialize: failed to open encoder session for model %1",
                            stdc::path::to_utf8(config->encoder));
            return res;
        }

        // Open pitch session (predictor)
        impl.predictorSession = impl.driver->createSession();
        auto predictorOpenArgs = srt::core::NO<Onnx::SessionOpenArgs>::create();
        predictorOpenArgs->useCpu = false;
        if (auto res = impl.predictorSession->open(config->predictor, predictorOpenArgs); !res) {
            setState(Failed);
            Log.srtCritical("[Pitch] initialize: failed to open predictor session for model %1",
                            stdc::path::to_utf8(config->predictor));
            return res;
        }

        // Initialize inference state
        setState(Idle);

        // return success
        return srt::core::Expected<void>();
    }

    srt::core::Expected<srt::core::NO<srt::core::TaskResult>> PitchInference::start(const srt::core::NO<srt::core::TaskStartInput> &input) {
        __stdc_impl_t;

        {
            std::shared_lock<std::shared_mutex> lock(impl.mutex);
            if (!impl.driver) {
                setState(Failed);
                Log.srtCritical("[Pitch] start: inference driver not initialized");
                return srt::core::Error(srt::core::Error::SessionError,
                                  "[Pitch] inference driver not initialized");
            }
        }

        setState(Running);

        // Get pitch config
        auto expConfig = getConfig(spec());
        if (!expConfig) {
            setState(Failed);
            Log.srtCritical("[Pitch] start: %1", expConfig.error().message());
            return expConfig.takeError();
        }
        const auto config = expConfig.take();

        if (!input) {
            setState(Failed);
            Log.srtCritical("[Pitch] start: input is nullptr");
            return srt::core::Error(srt::core::Error::InvalidArgument,
                              "[Pitch] input is nullptr");
        }

        if (const auto &name = input->objectName(); name != Pit::API_NAME) {
            setState(Failed);
            Log.srtCritical("[Pitch] start: invalid input name: expected %1, got %2",
                            Pit::API_NAME, name);
            return srt::core::Error(
                srt::core::Error::InvalidArgument,
                stdc::formatN(R"([Pitch] invalid input name: expected "%1", got "%2")",
                              Pit::API_NAME, name));
        }

        auto pitchInput = input.as<Pit::PitchStartInput>();
        // ...

        auto sessionInput = srt::core::NO<Onnx::SessionStartInput>::create();

        double frameWidth = config->frameWidth;
        if (!std::isfinite(frameWidth) || frameWidth <= 0) {
            setState(Failed);
            Log.srtCritical("[Pitch] start: frame width must be positive");
            return srt::core::Error(srt::core::Error::InvalidArgument,
                              "[Pitch] frame width must be positive");
        }

        // Part 1: Linguistic Encoder Inference
        {
            srt::core::NO<Onnx::SessionStartInput> linguisticInput;
            switch (config->linguisticMode) {
                case Co::LinguisticMode::LM_Word:
                    if (auto exp = ds::infer::inferutil::preprocessLinguisticWord(
                            pitchInput->words, config->phonemes, config->languages,
                            config->useLanguageId, frameWidth);
                        exp) {
                        linguisticInput = exp.take();
                    } else {
                        setState(Failed);
                        Log.srtCritical("[Pitch] start: preprocessLinguisticWord failed: %1",
                                        exp.error().message());
                        return exp.takeError();
                    }
                    break;
                case Co::LinguisticMode::LM_Phoneme:
                    if (auto exp = ds::infer::inferutil::preprocessLinguisticPhoneme(
                            pitchInput->words, config->phonemes, config->languages,
                            config->useLanguageId, frameWidth);
                        exp) {
                        linguisticInput = exp.take();
                    } else {
                        setState(Failed);
                        Log.srtCritical("[Pitch] start: preprocessLinguisticPhoneme failed: %1",
                                        exp.error().message());
                        return exp.takeError();
                    }
                    break;
                default:
                    setState(Failed);
                    Log.srtCritical("[Pitch] start: invalid LinguisticMode");
                    return srt::core::Error(srt::core::Error::SessionError,
                                      "[Pitch] invalid LinguisticMode");
            }

            // Run Linguistic Encoder Inference
            std::unique_lock<std::shared_mutex> lock(impl.mutex);
            if (!impl.encoderSession || !impl.encoderSession->isOpen()) {
                setState(Failed);
                Log.srtCritical("[Pitch] start: linguistic encoder session is not initialized");
                return srt::core::Error(srt::core::Error::SessionError,
                                  "[Pitch] linguistic encoder session is not initialized");
            }
            if (auto encoderSessionExp =
                    ds::infer::inferutil::runEncoder(impl.encoderSession, linguisticInput,
                                                  /* out */ sessionInput, false);
                !encoderSessionExp) {
                setState(Failed);
                Log.srtCritical("[Pitch] start: runEncoder failed: %1",
                                encoderSessionExp.error().message());
                return encoderSessionExp.takeError();
            }
        }

        // Part 2: Pitch Inference

        auto noteCount = ds::infer::inferutil::getNoteCount(pitchInput->words);

        std::vector<uint8_t> noteRest;
        std::vector<float> noteMidi;
        std::vector<int64_t> noteDur;
        noteRest.reserve(noteCount);
        noteMidi.reserve(noteCount);
        noteDur.reserve(noteCount);

        double noteDurSum = 0;
        for (const auto &word : pitchInput->words) {
            for (const auto &note : word.notes) {
                noteRest.emplace_back(note.is_rest ? 1 : 0);
                noteMidi.emplace_back(note.is_rest ? 0
                                                   : (static_cast<float>(note.key) +
                                                      static_cast<float>(note.cents) / 100.0f));
                int64_t noteDurPrevFrames = std::llround(noteDurSum / frameWidth);
                noteDurSum += note.duration;
                int64_t noteDurCurrFrames = std::llround(noteDurSum / frameWidth);
                noteDur.emplace_back(noteDurCurrFrames - noteDurPrevFrames);
            }
        }

        int64_t targetLength =
            std::accumulate(noteDur.begin(), noteDur.end(), int64_t{0}, std::plus<>());

        if (!ds::infer::inferutil::fillRestMidiWithNearestInPlace<float>(noteMidi, noteRest)) {
            setState(Failed);
            Log.srtCritical("[Pitch] start: failed to fill rest notes");
            return srt::core::Error(srt::core::Error::SessionError,
                              "[Pitch] failed to fill rest notes");
        }

        auto tensorFrom1DArray = [&](const auto &vec) {
            std::vector<int64_t> shape{1, static_cast<int64_t>(vec.size())};
            return srt::core::Tensor::createFromView(shape, stdc::array_view(vec));
        };

        if (auto exp = tensorFrom1DArray(noteMidi); exp) {
            sessionInput->inputs.emplace("note_midi", exp.take());
        } else {
            setState(Failed);
            Log.srtCritical("[Pitch] start: failed to create note_midi tensor: %1",
                            exp.error().message());
            return exp.takeError();
        }

        if (config->useRestFlags) {
            srt::core::Tensor::Container noteRestContainer(noteRest.size());
            std::transform(noteRest.begin(), noteRest.end(), noteRestContainer.begin(),
                           [](auto c) { return static_cast<std::byte>(c); });
            std::vector<int64_t> shape{1, static_cast<int64_t>(noteRestContainer.size())};
            auto exp =
                srt::core::Tensor::createFromRawData(srt::core::ITensor::Bool, shape, std::move(noteRestContainer));
            if (exp) {
                sessionInput->inputs.emplace("note_rest", exp.take());
            } else {
                setState(Failed);
                Log.srtCritical("[Pitch] start: failed to create note_rest tensor: %1",
                                exp.error().message());
                return exp.takeError();
            }
        }

        if (auto exp = tensorFrom1DArray(noteDur); exp) {
            sessionInput->inputs.emplace("note_dur", exp.take());
        } else {
            setState(Failed);
            Log.srtCritical("[Pitch] start: failed to create note_dur tensor: %1",
                            exp.error().message());
            return exp.takeError();
        }

        if (auto exp = ds::infer::inferutil::preprocessPhonemeDurations(pitchInput->words,
                                                                     config->frameWidth);
            exp) {
            sessionInput->inputs.emplace("ph_dur", exp.take());
        } else {
            setState(Failed);
            Log.srtCritical("[Pitch] start: preprocessPhonemeDurations failed: %1",
                            exp.error().message());
            return exp.takeError();
        }

        bool satisfyPitch = false;
        bool satisfyExpr = !config->useExpressiveness;
        for (const auto &param : pitchInput->parameters) {
            const auto isPitch = param.tag == Co::Tags::Pitch;
            const auto isExpr = param.tag == Co::Tags::Expr;
            if (!isPitch && !isExpr) {
                continue;
            }
            // Resample
            auto samples = ds::infer::inferutil::resample(param.values, param.interval, frameWidth,
                                                       targetLength, true);
            if (samples.size() != targetLength) {
                setState(Failed);
                Log.srtCritical("[Pitch] start: parameter %1 resample failed (size=%2, expected=%3)",
                                std::string(param.tag.name()), samples.size(), targetLength);
                return srt::core::Error(srt::core::Error::SessionError,
                                "[Pitch] parameter " +
                                std::string(param.tag.name()) +
                                " resample failed");
            }

            if (isPitch) {
                if (auto exp = srt::core::Tensor::create(srt::core::ITensor::Float, {1, targetLength}); exp) {
                    auto pitchTensor = exp.take();
                    if (pitchTensor->elementCount() != targetLength) {
                        setState(Failed);
                        Log.srtCritical("[Pitch] start: pitch tensor element count does not match target length");
                        return srt::core::Error(
                            srt::core::Error::SessionError,
                            "[Pitch] pitch tensor element count does not match target length");
                    }
                    auto pitchBuffer = pitchTensor->mutableData<float>();
                    if (!pitchBuffer) {
                        setState(Failed);
                        Log.srtCritical("[Pitch] start: failed to create pitch tensor");
                        return srt::core::Error(srt::core::Error::SessionError,
                                          "[Pitch] failed to create pitch tensor");
                    }
                    for (size_t i = 0; i < targetLength; ++i) {
                        pitchBuffer[i] = static_cast<float>(samples[i]);
                    }
                    sessionInput->inputs.emplace("pitch", std::move(pitchTensor));
                } else {
                    setState(Failed);
                    Log.srtCritical("[Pitch] start: failed to create pitch tensor: %1",
                                    exp.error().message());
                    return exp.takeError();
                }
                // Retake
                srt::core::Tensor::Container retake(targetLength, std::byte{1});
                if (param.retake.has_value()) {
                    const auto &[start, end] = *param.retake;
                    int64_t retakeStartFrame =
                        std::clamp<int64_t>(static_cast<int64_t>(std::llround(start / frameWidth)),
                                            int64_t{0}, targetLength);
                    int64_t retakeEndFrame =
                        std::clamp<int64_t>(static_cast<int64_t>(std::llround(end / frameWidth)),
                                            int64_t{0}, targetLength);
                    if (retakeStartFrame == retakeEndFrame) {
                        std::fill(retake.begin(), retake.end(), std::byte{0});
                    } else if (retakeStartFrame < retakeEndFrame) {
                        std::fill_n(retake.begin(), retakeStartFrame, std::byte{0});
                        std::fill(retake.begin() + retakeEndFrame, retake.end(), std::byte{0});
                    }
                }
                auto exp =
                    srt::core::Tensor::createFromRawData(srt::core::ITensor::Bool, {1, targetLength}, std::move(retake));
                if (exp) {
                    sessionInput->inputs.emplace("retake", exp.take());
                } else {
                    setState(Failed);
                    Log.srtCritical("[Pitch] start: failed to create retake tensor: %1",
                                    exp.error().message());
                    return exp.takeError();
                }
                satisfyPitch = true;
            } else if (!satisfyExpr && isExpr) {
                if (auto exp = srt::core::Tensor::create(srt::core::ITensor::Float, {1, targetLength}); exp) {
                    auto exprTensor = exp.take();
                    if (exprTensor->elementCount() != targetLength) {
                        setState(Failed);
                        Log.srtCritical("[Pitch] start: expr tensor element count does not match target length");
                        return srt::core::Error(srt::core::Error::SessionError,
                                          "[Pitch] expr tensor element count does not match target length");
                    }
                    auto exprBuffer = exprTensor->mutableData<float>();
                    if (!exprBuffer) {
                        setState(Failed);
                        Log.srtCritical("[Pitch] start: failed to create expr tensor");
                        return srt::core::Error(srt::core::Error::SessionError,
                                          "[Pitch] failed to create expr tensor");
                    }
                    for (size_t i = 0; i < targetLength; ++i) {
                        exprBuffer[i] = static_cast<float>(samples[i]);
                    }
                    sessionInput->inputs.emplace("expr", std::move(exprTensor));
                    satisfyExpr = true;
                } else {
                    setState(Failed);
                    Log.srtCritical("[Pitch] start: failed to create expr tensor: %1",
                                    exp.error().message());
                    return exp.takeError();
                }
            }
        }

        if (!satisfyPitch) {
            // No pitch supplied.
            // Will pass pitch tensor of all zeros and retake tensor of all true values.
            if (auto exp = srt::core::Tensor::createFilled<float>({1, targetLength}, 0.0f); exp) {
                sessionInput->inputs.emplace("pitch", exp.take());
            } else {
                setState(Failed);
                Log.srtCritical("[Pitch] start: failed to create pitch fallback tensor: %1",
                                exp.error().message());
                return exp.takeError();
            }
            if (auto exp = srt::core::Tensor::createFromRawData(srt::core::ITensor::Bool, {1, targetLength},
                                                     srt::core::Tensor::Container(targetLength, std::byte{1}));
                exp) {
                sessionInput->inputs.emplace("retake", exp.take());
                satisfyPitch = true;
            } else {
                setState(Failed);
                Log.srtCritical("[Pitch] start: failed to create retake fallback tensor: %1",
                                exp.error().message());
                return exp.takeError();
            }
        }

        if (!satisfyExpr) {
            // Model needs expr but no expr supplied.
            // Will use all ones instead.
            if (auto exp = srt::core::Tensor::createFilled<float>({1, targetLength}, 1.0f); exp) {
                sessionInput->inputs.emplace("expr", exp.take());
                satisfyExpr = true;
            } else {
                setState(Failed);
                Log.srtCritical("[Pitch] start: failed to create expr fallback tensor: %1",
                                exp.error().message());
                return exp.takeError();
            }
        }

        // Speaker embedding
        if (config->useSpeakerEmbedding) {
            if (pitchInput->speakers.empty()) {
                setState(Failed);
                Log.srtCritical("[Pitch] start: no speakers found in input");
                return srt::core::Error(srt::core::Error::SessionError,
                                  "[Pitch] no speakers found in input");
            }

            auto exp = ds::infer::inferutil::preprocessSpeakerEmbeddingFrames(
                pitchInput->speakers, config->speakers, config->hiddenSize, frameWidth,
                targetLength);
            if (exp) {
                sessionInput->inputs["spk_embed"] = exp.take();
            } else {
                setState(Failed);
                Log.srtCritical("[Pitch] start: preprocessSpeakerEmbeddingFrames failed: %1",
                                exp.error().message());
                return exp.takeError();
            }
        } else {
            // Nothing to do: speaker embedding is not supported
        }

        // input param: steps / speedup
        int64_t acceleration = pitchInput->steps;
        if (!config->useContinuousAcceleration) {
            acceleration = ds::infer::inferutil::getSpeedupFromSteps(acceleration);
        }
        {
            auto exp = srt::core::Tensor::createScalar<int64_t>(acceleration);
            if (!exp) {
                setState(Failed);
                Log.srtCritical("[Pitch] start: failed to create steps/speedup tensor: %1",
                                exp.error().message());
                return exp.takeError();
            }
            if (config->useContinuousAcceleration) {
                sessionInput->inputs["steps"] = exp.take();
            } else {
                sessionInput->inputs["speedup"] = exp.take();
            }
        }

        constexpr const char *outParamPitchPred = "pitch_pred";
        sessionInput->outputs.emplace(outParamPitchPred);

        std::unique_lock<std::shared_mutex> lock(impl.mutex);
        if (!impl.predictorSession || !impl.predictorSession->isOpen()) {
            setState(Failed);
            Log.srtCritical("[Pitch] start: predictor session is not initialized");
            return srt::core::Error(srt::core::Error::SessionError,
                              "[Pitch] predictor session is not initialized");
        }

        srt::core::NO<srt::core::TaskResult> sessionTaskResult;
        auto sessionExp = impl.predictorSession->start(sessionInput);
        if (!sessionExp) {
            setState(Failed);
            Log.srtCritical("[Pitch] start: predictor session->start failed: %1",
                            sessionExp.error().message());
            return sessionExp.takeError();
        } else {
            sessionTaskResult = sessionExp.take();
        }

        auto pitchResult = srt::core::NO<Pit::PitchResult>::create();

        // Get session results
        if (!sessionTaskResult) {
            setState(Failed);
            Log.srtCritical("[Pitch] start: predictor session result is nullptr");
            return srt::core::Error(srt::core::Error::SessionError,
                              "[Pitch] predictor session result is nullptr");
        }
        if (sessionTaskResult->objectName() != Onnx::API_NAME) {
            setState(Failed);
            Log.srtCritical("[Pitch] start: invalid result API name: %1",
                            sessionTaskResult->objectName());
            return srt::core::Error(srt::core::Error::InvalidArgument,
                              "[Pitch] invalid result API name");
        }
        auto sessionResult = sessionTaskResult.as<Onnx::SessionResult>();
        if (auto it_pred = sessionResult->outputs.find(outParamPitchPred);
            it_pred != sessionResult->outputs.end()) {
            // Extract onnx model result and copy to pitch final result vector (float -> double)
            auto output = std::move(it_pred->second);
            if (output->dataType() != srt::core::ITensor::Float) {
                setState(Failed);
                Log.srtCritical("[Pitch] start: model output is not float");
                return srt::core::Error(srt::core::Error::SessionError,
                                  "[Pitch] model output is not float");
            }
            const auto view = output->view<float>();
            if (view.empty()) {
                setState(Failed);
                Log.srtCritical("[Pitch] start: model output is empty");
                return srt::core::Error(srt::core::Error::SessionError,
                                  "[Pitch] model output is empty");
            }
            pitchResult->interval = frameWidth;
            pitchResult->pitch.assign(view.begin(), view.end());
        } else {
            setState(Failed);
            Log.srtCritical("[Pitch] start: output 'pitch_pred' not found in session result");
            return srt::core::Error(srt::core::Error::SessionError,
                              "[Pitch] output 'pitch_pred' not found in session result");
        }
        impl.result = pitchResult;

        setState(Idle);
        return pitchResult;
    }

    srt::core::Expected<void> PitchInference::startAsync(const srt::core::NO<srt::core::TaskStartInput> &input,
                                                   const StartAsyncCallback &callback) {
        // TODO:
        return srt::core::Error(srt::core::Error::NotImplemented);
    }

    bool PitchInference::stop() {
        __stdc_impl_t;
        bool flag = true;
        for (auto &session : {impl.encoderSession, impl.predictorSession}) {
            if (session) {
                flag &= session->stop();
            }
        }
        setState(Terminated);
        return flag;
    }

    srt::core::NO<srt::core::TaskResult> PitchInference::result() const {
        __stdc_impl_t;
        std::shared_lock<std::shared_mutex> lock(impl.mutex);
        return impl.result;
    }

}
