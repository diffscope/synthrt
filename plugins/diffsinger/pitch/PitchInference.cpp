#include "PitchInference.h"

#include <cmath>
#include <mutex>
#include <algorithm>
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
#include <inferutil/PluginCommon.h>

namespace srt::svs {

    namespace Co = Api::Common::L1;
    namespace Pit = Api::Pitch::L1;
    namespace Onnx = srt::driver::onnx;
    namespace DiffSinger = Api::DiffSinger::L1;

    static constexpr auto kLogPrefix = "[Pitch]";

    static srt::LogCategory Log("diffsinger.pitch");

    class PitchInference::Impl {
    public:
        srt::core::NO<Pit::PitchResult> result;
        srt::core::NO<srt::driver::InferenceDriver> driver;
        srt::core::NO<srt::driver::InferenceSession> encoderSession;
        srt::core::NO<srt::driver::InferenceSession> predictorSession;
        mutable std::shared_mutex mutex;
    };

    PitchInference::PitchInference(const srt::svs::InferenceSpec *spec)
        : Inference(spec), m_impl(std::make_unique<Impl>()) {
    }

    PitchInference::~PitchInference() = default;

    srt::core::Expected<void> PitchInference::initialize(const srt::core::NO<srt::core::TaskInitArgs> &args) {
        auto &impl = *m_impl;
        // Currently, no args to process. But we still need to enforce callers to pass the correct
        // args type.
        if (auto res = ds::infer::inferutil::validateInitArgs(args, Pit::API_NAME, kLogPrefix); !res) {
            setState(Failed);
            Log.srtCritical("%1 initialize: %2", kLogPrefix, res.error().message());
            return res.takeError();
        }
        auto pitchArgs = args.as<Pit::PitchInitArgs>();
        if (!pitchArgs) {
            setState(Failed);
            Log.srtCritical("%1 initialize: type mismatch, expected PitchInitArgs", kLogPrefix);
            return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceInputInvalid,
                              "[Pitch] type mismatch, expected PitchInitArgs",
                              {}, "pitch");
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

        // Get pitch config
        auto expConfig = ds::infer::inferutil::getTypedConfig<Pit::PitchConfiguration>(spec(), Pit::API_CLASS, Pit::API_NAME, kLogPrefix);
        if (!expConfig) {
            setState(Failed);
            Log.srtCritical("%1 initialize: %2", kLogPrefix, expConfig.error().message());
            return expConfig.takeError();
        }
        const auto config = expConfig.take();

        // Open pitch session (encoder)
        if (auto exp = ds::infer::inferutil::openOnnxSession(
                impl.driver, config->encoder, false, "encoder", kLogPrefix);
            exp) {
            impl.encoderSession = exp.take();
        } else {
            setState(Failed);
            Log.srtCritical("%1", exp.error().message());
            return exp.takeError();
        }

        // Open pitch session (predictor)
        if (auto exp = ds::infer::inferutil::openOnnxSession(
                impl.driver, config->predictor, false, "predictor", kLogPrefix);
            exp) {
            impl.predictorSession = exp.take();
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

    srt::core::Expected<srt::core::NO<srt::core::TaskResult>> PitchInference::start(const srt::core::NO<srt::core::TaskStartInput> &input) {
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

        // Get pitch config
        auto expConfig = ds::infer::inferutil::getTypedConfig<Pit::PitchConfiguration>(spec(), Pit::API_CLASS, Pit::API_NAME, kLogPrefix);
        if (!expConfig) {
            setState(Failed);
            Log.srtCritical("%1 start: %2", kLogPrefix, expConfig.error().message());
            return expConfig.takeError();
        }
        const auto config = expConfig.take();

        if (auto res = ds::infer::inferutil::validateStartInput(input, Pit::API_NAME, kLogPrefix); !res) {
            setState(Failed);
            Log.srtCritical("%1", res.error().message());
            return res.takeError();
        }

        auto pitchInput = input.as<Pit::PitchStartInput>();
        if (!pitchInput) {
            setState(Failed);
            Log.srtCritical("%1 start: type mismatch, expected PitchStartInput", kLogPrefix);
            return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceInputInvalid,
                              "[Pitch] type mismatch, expected PitchStartInput",
                              {}, "pitch");
        }
        // ...

        auto sessionInput = srt::core::NO<Onnx::SessionStartInput>::create();

        double frameWidth = config->frameWidth;
        if (auto res = ds::infer::inferutil::validateFrameWidth(frameWidth, kLogPrefix); !res) {
            setState(Failed);
            Log.srtCritical("%1", res.error().message());
            return res.takeError();
        }

        // Part 1: Linguistic Encoder Inference
        {
            // Auto-detect linguistic mode from ONNX model input names
            auto linguisticMode = config->linguisticMode;
            {
                // BUG-PLUGIN-PIT-01: Protect encoderSession access with a shared
                // lock so that initialize()/close() (which take a unique_lock)
                // cannot reassign or destroy encoderSession while inputNames()
                // and the returned reference are in use. The reference points
                // into the session's internals, so the lock must outlive the
                // use of `names`.
                std::shared_lock<std::shared_mutex> lock(impl.mutex);
                const auto &names = impl.encoderSession->inputNames();
                bool hasWordDiv = std::find(names.begin(), names.end(), "word_div") != names.end();
                bool hasPhDur = std::find(names.begin(), names.end(), "ph_dur") != names.end();
                if (hasWordDiv && linguisticMode != Co::LinguisticMode::LM_Word) {
                    Log.srtWarning("%1 start: model expects word mode, overriding", kLogPrefix);
                    linguisticMode = Co::LinguisticMode::LM_Word;
                } else if (!hasWordDiv && hasPhDur && linguisticMode != Co::LinguisticMode::LM_Phoneme) {
                    Log.srtWarning("%1 start: model expects phoneme mode, overriding", kLogPrefix);
                    linguisticMode = Co::LinguisticMode::LM_Phoneme;
                }
            }
            srt::core::NO<Onnx::SessionStartInput> linguisticInput;
            switch (linguisticMode) {
                case Co::LinguisticMode::LM_Word:
                    if (auto exp = ds::infer::inferutil::preprocessLinguisticWord(
                            pitchInput->words, config->phonemes, config->languages,
                            config->useLanguageId, frameWidth);
                        exp) {
                        linguisticInput = exp.take();
                    } else {
                        setState(Failed);
                        Log.srtCritical("%1 start: preprocessLinguisticWord failed: %2",
                                        kLogPrefix, exp.error().message());
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
                        Log.srtCritical("%1 start: preprocessLinguisticPhoneme failed: %2",
                                        kLogPrefix, exp.error().message());
                        return exp.takeError();
                    }
                    break;
                default:
                    setState(Failed);
                    Log.srtCritical("%1 start: invalid LinguisticMode", kLogPrefix);
                    return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceInputInvalid,
                                      "[Pitch] invalid LinguisticMode",
                                      {}, "pitch");
            }

            // Run Linguistic Encoder Inference
            std::unique_lock<std::shared_mutex> lock(impl.mutex);
            if (!impl.encoderSession || !impl.encoderSession->isOpen()) {
                setState(Failed);
                Log.srtCritical("%1 start: linguistic encoder session is not initialized", kLogPrefix);
                return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceStartFailed,
                                  "[Pitch] linguistic encoder session is not initialized",
                                  {}, "pitch");
            }
            if (auto encoderSessionExp =
                    ds::infer::inferutil::runEncoder(impl.encoderSession, linguisticInput,
                                                  /* out */ sessionInput, false);
                !encoderSessionExp) {
                setState(Failed);
                Log.srtCritical("%1 start: runEncoder failed: %2",
                                kLogPrefix, encoderSessionExp.error().message());
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
            Log.srtCritical("%1 start: failed to fill rest notes", kLogPrefix);
            return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceInputInvalid,
                              "[Pitch] failed to fill rest notes",
                              {}, "pitch");
        }

        auto tensorFrom1DArray = [&](const auto &vec) {
            std::vector<int64_t> shape{1, static_cast<int64_t>(vec.size())};
            return srt::core::Tensor::createFromView(shape, stdc::array_view(vec));
        };

        if (auto exp = tensorFrom1DArray(noteMidi); exp) {
            sessionInput->inputs.emplace("note_midi", exp.take());
        } else {
            setState(Failed);
            Log.srtCritical("%1 start: failed to create note_midi tensor: %2",
                            kLogPrefix, exp.error().message());
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
                Log.srtCritical("%1 start: failed to create note_rest tensor: %2",
                                kLogPrefix, exp.error().message());
                return exp.takeError();
            }
        }

        if (auto exp = tensorFrom1DArray(noteDur); exp) {
            sessionInput->inputs.emplace("note_dur", exp.take());
        } else {
            setState(Failed);
            Log.srtCritical("%1 start: failed to create note_dur tensor: %2",
                            kLogPrefix, exp.error().message());
            return exp.takeError();
        }

        if (auto exp = ds::infer::inferutil::preprocessPhonemeDurations(pitchInput->words,
                                                                     config->frameWidth);
            exp) {
            sessionInput->inputs.emplace("ph_dur", exp.take());
        } else {
            setState(Failed);
            Log.srtCritical("%1 start: preprocessPhonemeDurations failed: %2",
                            kLogPrefix, exp.error().message());
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
                Log.srtCritical("%1 start: parameter %2 resample failed (size=%3, expected=%4)",
                                kLogPrefix, std::string(param.tag.name()), samples.size(), targetLength);
                return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceInputInvalid,
                                "[Pitch] parameter " +
                                std::string(param.tag.name()) +
                                " resample failed", {}, "pitch");
            }

            if (isPitch) {
                if (auto exp = srt::core::Tensor::create(srt::core::ITensor::Float, {1, targetLength}); exp) {
                    auto pitchTensor = exp.take();
                    if (pitchTensor->elementCount() != targetLength) {
                        setState(Failed);
                        Log.srtCritical("%1 start: pitch tensor element count does not match target length", kLogPrefix);
                        return srt::core::Error::inferenceError(
                            srt::core::ErrorCode::InferenceTensorCreateFailed,
                            "[Pitch] pitch tensor element count does not match target length",
                            {}, "pitch");
                    }
                    auto pitchBuffer = pitchTensor->mutableData<float>();
                    if (!pitchBuffer) {
                        setState(Failed);
                        Log.srtCritical("%1 start: failed to create pitch tensor", kLogPrefix);
                        return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceTensorCreateFailed,
                                          "[Pitch] failed to create pitch tensor",
                                          {}, "pitch");
                    }
                    for (size_t i = 0; i < targetLength; ++i) {
                        pitchBuffer[i] = static_cast<float>(samples[i]);
                    }
                    sessionInput->inputs.emplace("pitch", std::move(pitchTensor));
                } else {
                    setState(Failed);
                    Log.srtCritical("%1 start: failed to create pitch tensor: %2",
                                    kLogPrefix, exp.error().message());
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
                    Log.srtCritical("%1 start: failed to create retake tensor: %2",
                                    kLogPrefix, exp.error().message());
                    return exp.takeError();
                }
                satisfyPitch = true;
            } else if (!satisfyExpr && isExpr) {
                if (auto exp = srt::core::Tensor::create(srt::core::ITensor::Float, {1, targetLength}); exp) {
                    auto exprTensor = exp.take();
                    if (exprTensor->elementCount() != targetLength) {
                        setState(Failed);
                        Log.srtCritical("%1 start: expr tensor element count does not match target length", kLogPrefix);
                        return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceTensorCreateFailed,
                                          "[Pitch] expr tensor element count does not match target length",
                                          {}, "pitch");
                    }
                    auto exprBuffer = exprTensor->mutableData<float>();
                    if (!exprBuffer) {
                        setState(Failed);
                        Log.srtCritical("%1 start: failed to create expr tensor", kLogPrefix);
                        return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceTensorCreateFailed,
                                          "[Pitch] failed to create expr tensor",
                                          {}, "pitch");
                    }
                    for (size_t i = 0; i < targetLength; ++i) {
                        exprBuffer[i] = static_cast<float>(samples[i]);
                    }
                    sessionInput->inputs.emplace("expr", std::move(exprTensor));
                    satisfyExpr = true;
                } else {
                    setState(Failed);
                    Log.srtCritical("%1 start: failed to create expr tensor: %2",
                                    kLogPrefix, exp.error().message());
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
                Log.srtCritical("%1 start: failed to create pitch fallback tensor: %2",
                                kLogPrefix, exp.error().message());
                return exp.takeError();
            }
            if (auto exp = srt::core::Tensor::createFromRawData(srt::core::ITensor::Bool, {1, targetLength},
                                                     srt::core::Tensor::Container(targetLength, std::byte{1}));
                exp) {
                sessionInput->inputs.emplace("retake", exp.take());
                satisfyPitch = true;
            } else {
                setState(Failed);
                Log.srtCritical("%1 start: failed to create retake fallback tensor: %2",
                                kLogPrefix, exp.error().message());
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
                Log.srtCritical("%1 start: failed to create expr fallback tensor: %2",
                                kLogPrefix, exp.error().message());
                return exp.takeError();
            }
        }

        // Speaker embedding
        if (config->useSpeakerEmbedding) {
            if (pitchInput->speakers.empty()) {
                setState(Failed);
                Log.srtCritical("%1 start: no speakers found in input", kLogPrefix);
                return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceSpeakerNotFound,
                                  "[Pitch] no speakers found in input",
                                  {}, "pitch");
            }

            auto exp = ds::infer::inferutil::preprocessSpeakerEmbeddingFrames(
                pitchInput->speakers, config->speakers, config->hiddenSize, frameWidth,
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

        // input param: steps / speedup
        int64_t acceleration = pitchInput->steps;
        if (!config->useContinuousAcceleration) {
            acceleration = ds::infer::inferutil::getSpeedupFromSteps(acceleration);
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

        constexpr const char *outParamPitchPred = "pitch_pred";
        sessionInput->outputs.emplace(outParamPitchPred);

        std::unique_lock<std::shared_mutex> lock(impl.mutex);
        if (!impl.predictorSession || !impl.predictorSession->isOpen()) {
            setState(Failed);
            Log.srtCritical("%1 start: predictor session is not initialized", kLogPrefix);
            return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceStartFailed,
                              "[Pitch] predictor session is not initialized",
                              {}, "pitch");
        }

        srt::core::NO<srt::core::TaskResult> sessionTaskResult;
        auto sessionExp = impl.predictorSession->start(sessionInput);
        if (!sessionExp) {
            setState(Failed);
            Log.srtCritical("%1 start: predictor session->start failed: %2",
                            kLogPrefix, sessionExp.error().message());
            return sessionExp.takeError();
        } else {
            sessionTaskResult = sessionExp.take();
        }

        auto pitchResult = srt::core::NO<Pit::PitchResult>::create();

        // Get session results
        if (!sessionTaskResult) {
            setState(Failed);
            Log.srtCritical("%1 start: predictor session result is nullptr", kLogPrefix);
            return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceOutputEmpty,
                              "[Pitch] predictor session result is nullptr",
                              {}, "pitch");
        }
        if (sessionTaskResult->objectName() != Onnx::API_NAME) {
            setState(Failed);
            Log.srtCritical("%1 start: invalid result API name: %2",
                            kLogPrefix, sessionTaskResult->objectName());
            return srt::core::Error::inferenceError(srt::core::ErrorCode::InvalidArgument,
                              "[Pitch] invalid result API name",
                              {}, "pitch");
        }
        auto sessionResult = sessionTaskResult.as<Onnx::SessionResult>();
        if (!sessionResult) {
            setState(Failed);
            Log.srtCritical("%1 start: type mismatch, expected SessionResult", kLogPrefix);
            return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceRunFailed,
                              "[Pitch] type mismatch, expected SessionResult",
                              {}, "pitch");
        }
        if (auto it_pred = sessionResult->outputs.find(outParamPitchPred);
            it_pred != sessionResult->outputs.end()) {
            // Extract onnx model result and copy to pitch final result vector (float -> double)
            auto output = std::move(it_pred->second);
            if (output->dataType() != srt::core::ITensor::Float) {
                setState(Failed);
                Log.srtCritical("%1 start: model output is not float", kLogPrefix);
                return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceDataTypeMismatch,
                                  "[Pitch] model output is not float",
                                  {}, "pitch");
            }
            const auto view = output->view<float>();
            if (view.empty()) {
                setState(Failed);
                Log.srtCritical("%1 start: model output is empty", kLogPrefix);
                return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceOutputEmpty,
                                  "[Pitch] model output is empty",
                                  {}, "pitch");
            }
            pitchResult->interval = frameWidth;
            pitchResult->pitch.assign(view.begin(), view.end());
        } else {
            setState(Failed);
            Log.srtCritical("%1 start: output 'pitch_pred' not found in session result", kLogPrefix);
            return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceOutputEmpty,
                              "[Pitch] output 'pitch_pred' not found in session result",
                              {}, "pitch");
        }
        impl.result = pitchResult;

        setState(Idle);
        return pitchResult;
    }

    srt::core::Expected<void> PitchInference::startAsync(const srt::core::NO<srt::core::TaskStartInput> &input,
                                                   const StartAsyncCallback &callback) {
        // TODO:
        return srt::core::Error(srt::core::ErrorCode::NotImplemented, "not implemented");
    }

    bool PitchInference::stop() {
        auto &impl = *m_impl;
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
        auto &impl = *m_impl;
        std::shared_lock<std::shared_mutex> lock(impl.mutex);
        return impl.result;
    }

}
