#include "PitchInference.h"

#include <cmath>
#include <mutex>
#include <numeric>
#include <shared_mutex>
#include <utility>

#include <stdcorelib/pimpl.h>
#include <stdcorelib/str.h>
#include <stdcorelib/path.h>

#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>
#include <dsinfer/Api/Inferences/Pitch/1/PitchApiL1.h>
#include <dsinfer/Api/Drivers/Onnx/OnnxDriverApi.h>
#include <dsinfer/Api/Singers/DiffSinger/1/DiffSingerApiL1.h>
#include <dsinfer/Inference/InferenceDriver.h>
#include <dsinfer/Inference/InferenceSession.h>
#include <dsinfer/Core/Tensor.h>

#include <inferutil/Driver.h>
#include <inferutil/Algorithm.h>
#include <inferutil/InputWord.h>
#include <inferutil/LinguisticEncoder.h>
#include <inferutil/SpeakerEmbedding.h>
#include <inferutil/Speedup.h>

namespace ds {

    namespace Co = Api::Common::L1;
    namespace Pit = Api::Pitch::L1;
    namespace Onnx = Api::Onnx;
    namespace DiffSinger = Api::DiffSinger::L1;

    static inline srt::Expected<srt::NO<Pit::PitchConfiguration>>
        getConfig(const srt::InferenceSpec *spec) {

        const auto genericConfig = spec->configuration();
        if (!genericConfig) {
            return srt::Error(srt::Error::InvalidArgument, "pitch configuration is nullptr");
        }
        if (!(genericConfig->className() == Pit::API_CLASS &&
              genericConfig->objectName() == Pit::API_NAME)) {
            return srt::Error(srt::Error::InvalidArgument, "invalid pitch configuration");
        }
        return genericConfig.as<Pit::PitchConfiguration>();
    }

    class PitchInference::Impl {
    public:
        srt::NO<Pit::PitchResult> result;
        srt::NO<InferenceDriver> driver;
        srt::NO<InferenceSession> encoderSession;
        srt::NO<InferenceSession> predictorSession;
        mutable std::shared_mutex mutex;
    };

    PitchInference::PitchInference(const srt::InferenceSpec *spec)
        : Inference(spec), _impl(std::make_unique<Impl>()) {
    }

    PitchInference::~PitchInference() = default;

    srt::Expected<void> PitchInference::initialize(const srt::NO<srt::TaskInitArgs> &args) {
        __stdc_impl_t;
        // Currently, no args to process. But we still need to enforce callers to pass the correct
        // args type.
        if (!args) {
            return srt::Error(srt::Error::InvalidArgument, "pitch task init args is nullptr");
        }
        if (auto name = args->objectName(); name != Pit::API_NAME) {
            return srt::Error(
                srt::Error::InvalidArgument,
                stdc::formatN(R"(invalid pitch task init args name: expected "%1", got "%2")",
                              Pit::API_NAME, name));
        }
        auto pitchArgs = args.as<Pit::PitchInitArgs>();

        std::unique_lock<std::shared_mutex> lock(impl.mutex);

        // If there are existing result, they will be cleared.
        impl.result.reset();

        if (auto res = inferutil::getInferenceDriver(this); res) {
            impl.driver = res.take();
        } else {
            setState(Failed);
            return res.takeError();
        }

        // Get pitch config
        auto expConfig = getConfig(spec());
        if (!expConfig) {
            setState(Failed);
            return expConfig.takeError();
        }
        const auto config = expConfig.take();

        // Open pitch session (encoder)
        impl.encoderSession = impl.driver->createSession();
        auto encoderOpenArgs = srt::NO<Onnx::SessionOpenArgs>::create();
        encoderOpenArgs->useCpu = false;
        if (auto res = impl.encoderSession->open(config->encoder, encoderOpenArgs); !res) {
            setState(Failed);
            return res;
        }

        // Open pitch session (predictor)
        impl.predictorSession = impl.driver->createSession();
        auto predictorOpenArgs = srt::NO<Onnx::SessionOpenArgs>::create();
        predictorOpenArgs->useCpu = false;
        if (auto res = impl.predictorSession->open(config->predictor, predictorOpenArgs); !res) {
            setState(Failed);
            return res;
        }

        // Initialize inference state
        setState(Idle);

        // return success
        return srt::Expected<void>();
    }

    srt::Expected<srt::NO<srt::TaskResult>> PitchInference::start(const srt::NO<srt::TaskStartInput> &input) {
        __stdc_impl_t;

        {
            std::shared_lock<std::shared_mutex> lock(impl.mutex);
            if (!impl.driver) {
                setState(Failed);
                return srt::Error(srt::Error::SessionError, "inference driver not initialized");
            }
        }

        setState(Running);

        // Get pitch config
        auto expConfig = getConfig(spec());
        if (!expConfig) {
            setState(Failed);
            return expConfig.takeError();
        }
        const auto config = expConfig.take();

        if (!input) {
            setState(Failed);
            return srt::Error(srt::Error::InvalidArgument, "pitch input is nullptr");
        }

        if (const auto &name = input->objectName(); name != Pit::API_NAME) {
            setState(Failed);
            return srt::Error(
                srt::Error::InvalidArgument,
                stdc::formatN(R"(invalid pitch task init args name: expected "%1", got "%2")",
                              Pit::API_NAME, name));
        }

        auto pitchInput = input.as<Pit::PitchStartInput>();
        // ...

        auto sessionInput = srt::NO<Onnx::SessionStartInput>::create();

        double frameWidth = config->frameWidth;
        if (!std::isfinite(frameWidth) || frameWidth <= 0) {
            setState(Failed);
            return srt::Error(srt::Error::InvalidArgument, "frame width must be positive");
        }

        // Part 1: Linguistic Encoder Inference
        {
            srt::NO<Onnx::SessionStartInput> linguisticInput;
            switch (config->linguisticMode) {
                case Co::LinguisticMode::LM_Word:
                    if (auto exp = inferutil::preprocessLinguisticWord(
                            pitchInput->words, config->phonemes, config->languages,
                            config->useLanguageId, frameWidth);
                        exp) {
                        linguisticInput = exp.take();
                    } else {
                        setState(Failed);
                        return exp.takeError();
                    }
                    break;
                case Co::LinguisticMode::LM_Phoneme:
                    if (auto exp = inferutil::preprocessLinguisticPhoneme(
                            pitchInput->words, config->phonemes, config->languages,
                            config->useLanguageId, frameWidth);
                        exp) {
                        linguisticInput = exp.take();
                    } else {
                        setState(Failed);
                        return exp.takeError();
                    }
                    break;
                default:
                    setState(Failed);
                    return srt::Error(srt::Error::SessionError, "invalid LinguisticMode");
            }

            // Run Linguistic Encoder Inference
            std::unique_lock<std::shared_mutex> lock(impl.mutex);
            if (!impl.encoderSession || !impl.encoderSession->isOpen()) {
                setState(Failed);
                return srt::Error(srt::Error::SessionError,
                                  "pitch linguistic encoder session is not initialized");
            }
            if (auto encoderSessionExp =
                    inferutil::runEncoder(impl.encoderSession, linguisticInput,
                                                  /* out */ sessionInput, false);
                !encoderSessionExp) {
                setState(Failed);
                return encoderSessionExp.takeError();
            }
        }

        // Part 2: Pitch Inference

        auto noteCount = inferutil::getNoteCount(pitchInput->words);

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

        if (!inferutil::fillRestMidiWithNearestInPlace<float>(noteMidi, noteRest)) {
            return srt::Error(srt::Error::SessionError, "failed to fill rest notes");
        }

        auto tensorFrom1DArray = [&](const auto &vec) {
            std::vector<int64_t> shape{1, static_cast<int64_t>(vec.size())};
            return Tensor::createFromView(shape, stdc::array_view(vec));
        };

        if (auto exp = tensorFrom1DArray(noteMidi); exp) {
            sessionInput->inputs.emplace("note_midi", exp.take());
        } else {
            setState(Failed);
            return exp.takeError();
        }

        if (config->useRestFlags) {
            Tensor::Container noteRestContainer(noteRest.size());
            std::transform(noteRest.begin(), noteRest.end(), noteRestContainer.begin(),
                           [](auto c) { return static_cast<std::byte>(c); });
            std::vector<int64_t> shape{1, static_cast<int64_t>(noteRestContainer.size())};
            auto exp =
                Tensor::createFromRawData(ITensor::Bool, shape, std::move(noteRestContainer));
            if (exp) {
                sessionInput->inputs.emplace("note_rest", exp.take());
            } else {
                setState(Failed);
                return exp.takeError();
            }
        }

        if (auto exp = tensorFrom1DArray(noteDur); exp) {
            sessionInput->inputs.emplace("note_dur", exp.take());
        } else {
            setState(Failed);
            return exp.takeError();
        }

        if (auto exp = inferutil::preprocessPhonemeDurations(pitchInput->words,
                                                                     config->frameWidth);
            exp) {
            sessionInput->inputs.emplace("ph_dur", exp.take());
        } else {
            setState(Failed);
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
            auto samples = inferutil::resample(param.values, param.interval, frameWidth,
                                                       targetLength, true);
            if (samples.size() != targetLength) {
                setState(Failed);
                return srt::Error(srt::Error::SessionError, "parameter " +
                                                                std::string(param.tag.name()) +
                                                                " resample failed");
            }

            if (isPitch) {
                if (auto exp = Tensor::create(ITensor::Float, {1, targetLength}); exp) {
                    auto pitchTensor = exp.take();
                    if (pitchTensor->elementCount() != targetLength) {
                        setState(Failed);
                        return srt::Error(
                            srt::Error::SessionError,
                            "pitch tensor element count does not match target length");
                    }
                    auto pitchBuffer = pitchTensor->mutableData<float>();
                    if (!pitchBuffer) {
                        setState(Failed);
                        return srt::Error(srt::Error::SessionError,
                                          "failed to create pitch tensor");
                    }
                    for (size_t i = 0; i < targetLength; ++i) {
                        pitchBuffer[i] = static_cast<float>(samples[i]);
                    }
                    sessionInput->inputs.emplace("pitch", std::move(pitchTensor));
                } else {
                    setState(Failed);
                    return exp.takeError();
                }
                // Retake
                Tensor::Container retake(targetLength, std::byte{1});
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
                    Tensor::createFromRawData(ITensor::Bool, {1, targetLength}, std::move(retake));
                if (exp) {
                    sessionInput->inputs.emplace("retake", exp.take());
                } else {
                    setState(Failed);
                    return exp.takeError();
                }
                satisfyPitch = true;
            } else if (!satisfyExpr && isExpr) {
                if (auto exp = Tensor::create(ITensor::Float, {1, targetLength}); exp) {
                    auto exprTensor = exp.take();
                    if (exprTensor->elementCount() != targetLength) {
                        setState(Failed);
                        return srt::Error(srt::Error::SessionError,
                                          "expr tensor element count does not match target length");
                    }
                    auto exprBuffer = exprTensor->mutableData<float>();
                    if (!exprBuffer) {
                        setState(Failed);
                        return srt::Error(srt::Error::SessionError, "failed to create expr tensor");
                    }
                    for (size_t i = 0; i < targetLength; ++i) {
                        exprBuffer[i] = static_cast<float>(samples[i]);
                    }
                    sessionInput->inputs.emplace("expr", std::move(exprTensor));
                    satisfyExpr = true;
                } else {
                    setState(Failed);
                    return exp.takeError();
                }
            }
        }

        if (!satisfyPitch) {
            // No pitch supplied.
            // Will pass pitch tensor of all zeros and retake tensor of all true values.
            if (auto exp = Tensor::createFilled<float>({1, targetLength}, 0.0f); exp) {
                sessionInput->inputs.emplace("pitch", exp.take());
            } else {
                setState(Failed);
                return exp.takeError();
            }
            if (auto exp = Tensor::createFromRawData(ITensor::Bool, {1, targetLength},
                                                     Tensor::Container(targetLength, std::byte{1}));
                exp) {
                sessionInput->inputs.emplace("retake", exp.take());
                satisfyPitch = true;
            } else {
                setState(Failed);
                return exp.takeError();
            }
        }

        if (!satisfyExpr) {
            // Model needs expr but no expr supplied.
            // Will use all ones instead.
            if (auto exp = Tensor::createFilled<float>({1, targetLength}, 1.0f); exp) {
                sessionInput->inputs.emplace("expr", exp.take());
                satisfyExpr = true;
            } else {
                setState(Failed);
                return exp.takeError();
            }
        }

        // Speaker embedding
        if (config->useSpeakerEmbedding) {
            if (pitchInput->speakers.empty()) {
                setState(Failed);
                return srt::Error(srt::Error::SessionError, "no speakers found in pitch input");
            }

            auto exp = inferutil::preprocessSpeakerEmbeddingFrames(
                pitchInput->speakers, config->speakers, config->hiddenSize, frameWidth,
                targetLength);
            if (exp) {
                sessionInput->inputs["spk_embed"] = exp.take();
            } else {
                setState(Failed);
                return exp.takeError();
            }
        } else {
            // Nothing to do: speaker embedding is not supported
        }

        // input param: steps / speedup
        int64_t acceleration = pitchInput->steps;
        if (!config->useContinuousAcceleration) {
            acceleration = inferutil::getSpeedupFromSteps(acceleration);
        }
        {
            auto exp = Tensor::createScalar<int64_t>(acceleration);
            if (!exp) {
                setState(Failed);
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
            return srt::Error(srt::Error::SessionError,
                              "pitch predictor session is not initialized");
        }

        srt::NO<srt::TaskResult> sessionTaskResult;
        auto sessionExp = impl.predictorSession->start(sessionInput);
        if (!sessionExp) {
            setState(Failed);
            return sessionExp.takeError();
        } else {
            sessionTaskResult = sessionExp.take();
        }

        auto pitchResult = srt::NO<Pit::PitchResult>::create();

        // Get session results
        if (!sessionTaskResult) {
            setState(Failed);
            return srt::Error(srt::Error::SessionError,
                              "pitch predictor session result is nullptr");
        }
        if (sessionTaskResult->objectName() != Onnx::API_NAME) {
            setState(Failed);
            return srt::Error(srt::Error::InvalidArgument, "invalid result API name");
        }
        auto sessionResult = sessionTaskResult.as<Onnx::SessionResult>();
        if (auto it_pred = sessionResult->outputs.find(outParamPitchPred);
            it_pred != sessionResult->outputs.end()) {
            // Extract onnx model result and copy to pitch final result vector (float -> double)
            auto output = std::move(it_pred->second);
            if (output->dataType() != ITensor::Float) {
                setState(Failed);
                return srt::Error(srt::Error::SessionError, "model output is not float");
            }
            const auto view = output->view<float>();
            if (view.empty()) {
                setState(Failed);
                return srt::Error(srt::Error::SessionError, "model output is empty");
            }
            pitchResult->interval = frameWidth;
            pitchResult->pitch.assign(view.begin(), view.end());
        } else {
            setState(Failed);
            return srt::Error(srt::Error::SessionError, "invalid result output");
        }
        impl.result = pitchResult;

        setState(Idle);
        return pitchResult;
    }

    srt::Expected<void> PitchInference::startAsync(const srt::NO<srt::TaskStartInput> &input,
                                                   const StartAsyncCallback &callback) {
        // TODO:
        return srt::Error(srt::Error::NotImplemented);
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

    srt::NO<srt::TaskResult> PitchInference::result() const {
        __stdc_impl_t;
        std::shared_lock<std::shared_mutex> lock(impl.mutex);
        return impl.result;
    }

}