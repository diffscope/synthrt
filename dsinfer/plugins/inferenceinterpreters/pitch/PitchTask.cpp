#include "PitchTask.h"

#include <cmath>
#include <mutex>
#include <numeric>
#include <shared_mutex>
#include <utility>

#include <stdcorelib/str.h>
#include <stdcorelib/path.h>

#include <dsinfer/Support/ErrorCode.h>
#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>
#include <dsinfer/Api/Inferences/Pitch/1/PitchApiL1.h>
#include <dsinfer/Api/Drivers/Onnx/OnnxDriverApi.h>
#include <dsinfer/Inference/InferenceDriver.h>
#include <dsinfer/Inference/InferenceSession.h>
#include <dsinfer/Core/Tensor.h>

#include <inferutil/Driver.h>
#include <inferutil/Algorithm.h>
#include <inferutil/InputWord.h>
#include <inferutil/LinguisticEncoder.h>
#include <inferutil/SpeakerEmbedding.h>
#include <inferutil/Speedup.h>

#include "PitchInference.h"

namespace ds {

    namespace Co = Api::Common::L1;
    namespace Pit = Api::Pitch::L1;
    namespace Onnx = Api::Onnx;

    static inline srt::Expected<const Pit::PitchConfiguration *>
        getConfig(const srt::InferenceSpec &spec) {

        const auto genericConfig = spec.configuration();
        if (!genericConfig) {
            return srt::Error(srt::Error::InvalidArgument, "pitch configuration is nullptr");
        }
        if (genericConfig->interface() != Pit::API_INTERFACE ||
            genericConfig->variant() != Pit::API_VARIANT ||
            genericConfig->level() != Pit::API_LEVEL) {
            return srt::Error(srt::Error::InvalidArgument, "invalid pitch configuration");
        }
        return static_cast<const Pit::PitchConfiguration *>(genericConfig);
    }

    PitchTask::PitchTask(PitchInference &inference) : m_inference(&inference) {
    }

    PitchTask::~PitchTask() = default;

    srt::Expected<void> PitchTask::initialize(const Pit::PitchInitArgs &args) {
        return initialize(static_cast<const srt::TaskInitArgs &>(args));
    }

    srt::Expected<std::unique_ptr<Pit::PitchResult>>
        PitchTask::start(const Pit::PitchStartInput &input) {
        auto result = start(static_cast<const srt::TaskStartInput &>(input));
        if (!result) {
            return result.takeError();
        }
        return std::unique_ptr<Pit::PitchResult>(
            static_cast<Pit::PitchResult *>(result.take().release()));
    }

    srt::Expected<void> PitchTask::startAsync(std::shared_ptr<const Pit::PitchStartInput> input,
                                              Pit::PitchExecInstance::AsyncCallback callback) {
        auto genericInput = std::static_pointer_cast<const srt::TaskStartInput>(std::move(input));
        return startAsync(std::move(genericInput),
                          [callback = std::move(callback)](
                              srt::Expected<std::unique_ptr<srt::TaskResult>> result) mutable {
                              if (!result) {
                                  callback(result.takeError());
                                  return;
                              }
                              callback(std::unique_ptr<Pit::PitchResult>(
                                  static_cast<Pit::PitchResult *>(result.take().release())));
                          });
    }

    srt::Expected<void> PitchTask::initialize(const srt::TaskInitArgs &args) {
        if (args.type() != Pit::API_INTERFACE || args.version() != Pit::API_LEVEL) {
            return srt::Error(
                srt::Error::InvalidArgument,
                stdc::formatN(
                    R"(invalid pitch initialization payload: expected "%1" level %2, got "%3" level %4)",
                    Pit::API_INTERFACE, Pit::API_LEVEL, args.type(), args.version()));
        }

        std::unique_lock<std::shared_mutex> lock(m_mutex);

        if (auto res = inferutil::getInferenceDriver(m_inference); res) {
            m_driver = res.take();
        } else {
            ITask::setState(ITask::Failed);
            return res.takeError();
        }

        auto expConfig = getConfig(m_inference->spec());
        if (!expConfig) {
            ITask::setState(ITask::Failed);
            return expConfig.takeError();
        }
        const auto config = expConfig.take();

        // Open pitch session (encoder)
        m_encoderSession = m_driver->createSession();
        Onnx::SessionOpenArgs encoderOpenArgs;
        encoderOpenArgs.useCpu = false;
        if (auto res = m_encoderSession->open(config->encoder, encoderOpenArgs); !res) {
            ITask::setState(ITask::Failed);
            return res;
        }

        // Open pitch session (predictor)
        m_predictorSession = m_driver->createSession();
        Onnx::SessionOpenArgs predictorOpenArgs;
        predictorOpenArgs.useCpu = false;
        if (auto res = m_predictorSession->open(config->predictor, predictorOpenArgs); !res) {
            ITask::setState(ITask::Failed);
            return res;
        }

        ITask::setState(ITask::Idle);
        return {};
    }

    srt::Expected<std::unique_ptr<srt::TaskResult>>
        PitchTask::start(const srt::TaskStartInput &input) {

        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            if (!m_driver) {
                ITask::setState(ITask::Failed);
                return srt::Error(ds::ErrorCode::NotInitialized,
                                  "inference driver not initialized");
            }
        }

        ITask::setState(ITask::Running);

        auto expConfig = getConfig(m_inference->spec());
        if (!expConfig) {
            ITask::setState(ITask::Failed);
            return expConfig.takeError();
        }
        const auto config = expConfig.take();


        if (input.type() != Pit::API_INTERFACE || input.version() != Pit::API_LEVEL) {
            ITask::setState(ITask::Failed);
            return srt::Error(
                srt::Error::InvalidArgument,
                stdc::formatN(
                    R"(invalid pitch start payload: expected "%1" level %2, got "%3" level %4)",
                    Pit::API_INTERFACE, Pit::API_LEVEL, input.type(), input.version()));
        }

        const auto &pitchInput = *input.as<Pit::PitchStartInput>();

        auto sessionInput = std::make_shared<Onnx::SessionStartInput>();

        double frameWidth = config->frameWidth;
        if (!std::isfinite(frameWidth) || frameWidth <= 0) {
            ITask::setState(ITask::Failed);
            return srt::Error(srt::Error::InvalidArgument, "frame width must be positive");
        }

        // Run the linguistic encoder.
        {
            std::shared_ptr<Onnx::SessionStartInput> linguisticInput;
            switch (config->linguisticMode) {
                case Co::LinguisticMode::Word:
                    if (auto exp = inferutil::preprocessLinguisticWord(
                            pitchInput.words, config->phonemes, config->languages,
                            config->useLanguageId, frameWidth);
                        exp) {
                        linguisticInput = exp.take();
                    } else {
                        ITask::setState(ITask::Failed);
                        return exp.takeError().withContext(
                            "failed to build the linguistic word input");
                    }
                    break;
                case Co::LinguisticMode::Phoneme:
                    if (auto exp = inferutil::preprocessLinguisticPhoneme(
                            pitchInput.words, config->phonemes, config->languages,
                            config->useLanguageId, frameWidth);
                        exp) {
                        linguisticInput = exp.take();
                    } else {
                        ITask::setState(ITask::Failed);
                        return exp.takeError().withContext(
                            "failed to build the linguistic phoneme input");
                    }
                    break;
                default:
                    ITask::setState(ITask::Failed);
                    return srt::Error(ds::ErrorCode::InvalidInput, "invalid LinguisticMode");
            }

            // Run Linguistic Encoder Inference
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            if (!m_encoderSession || !m_encoderSession->isOpen()) {
                ITask::setState(ITask::Failed);
                return srt::Error(ds::ErrorCode::NotInitialized,
                                  "pitch linguistic encoder session is not initialized");
            }
            if (auto encoderSessionExp =
                    inferutil::runEncoder(m_encoderSession.get(), *linguisticInput,
                                          /* out */ sessionInput, false);
                !encoderSessionExp) {
                ITask::setState(ITask::Failed);
                return encoderSessionExp.takeError().withContext("the linguistic encoder failed");
            }
        }

        // Prepare and run the pitch predictor.

        auto noteCount = inferutil::getNoteCount(pitchInput.words);

        std::vector<uint8_t> noteRest;
        std::vector<float> noteMidi;
        std::vector<int64_t> noteDur;
        noteRest.reserve(noteCount);
        noteMidi.reserve(noteCount);
        noteDur.reserve(noteCount);

        double noteDurSum = 0;
        for (const auto &word : pitchInput.words) {
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
            return srt::Error(ds::ErrorCode::ProcessingFailed, "failed to fill rest notes");
        }

        auto tensorFrom1DArray = [&](const auto &vec) {
            std::vector<int64_t> shape{1, static_cast<int64_t>(vec.size())};
            return Tensor::createFromView(shape, stdc::array_view(vec));
        };

        if (auto exp = tensorFrom1DArray(noteMidi); exp) {
            sessionInput->inputs.emplace("note_midi", exp.take());
        } else {
            ITask::setState(ITask::Failed);
            return exp.takeError().withContext(R"(failed to build the "note_midi" input)");
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
                ITask::setState(ITask::Failed);
                return exp.takeError().withContext(R"(failed to build the "note_rest" input)");
            }
        }

        if (auto exp = tensorFrom1DArray(noteDur); exp) {
            sessionInput->inputs.emplace("note_dur", exp.take());
        } else {
            ITask::setState(ITask::Failed);
            return exp.takeError().withContext(R"(failed to build the "note_dur" input)");
        }

        if (auto exp = inferutil::preprocessPhonemeDurations(pitchInput.words, config->frameWidth);
            exp) {
            sessionInput->inputs.emplace("ph_dur", exp.take());
        } else {
            ITask::setState(ITask::Failed);
            return exp.takeError().withContext(R"(failed to build the "ph_dur" input)");
        }

        bool satisfyPitch = false;
        bool satisfyExpr = !config->useExpressiveness;
        for (const auto &param : pitchInput.parameters) {
            const auto isPitch = param.tag == Co::Tags::Pitch;
            const auto isExpr = param.tag == Co::Tags::Expr;
            if (!isPitch && !isExpr) {
                continue;
            }
            // Resample
            auto samples =
                inferutil::resample(param.values, param.interval, frameWidth, targetLength, true);
            if (samples.size() != targetLength) {
                ITask::setState(ITask::Failed);
                return srt::Error(ds::ErrorCode::ProcessingFailed,
                                  "parameter " + std::string(param.tag.name()) +
                                      " resample failed");
            }

            if (isPitch) {
                if (auto exp = Tensor::create(ITensor::Float, {1, targetLength}); exp) {
                    auto pitchTensor = exp.take();
                    if (pitchTensor->elementCount() != targetLength) {
                        ITask::setState(ITask::Failed);
                        return srt::Error(
                            ds::ErrorCode::ShapeMismatch,
                            "pitch tensor element count does not match target length");
                    }
                    auto pitchBuffer = pitchTensor->data<float>();
                    if (!pitchBuffer) {
                        ITask::setState(ITask::Failed);
                        return srt::Error(ds::ErrorCode::ProcessingFailed,
                                          "failed to create pitch tensor");
                    }
                    for (size_t i = 0; i < targetLength; ++i) {
                        pitchBuffer[i] = static_cast<float>(samples[i]);
                    }
                    sessionInput->inputs.emplace("pitch", std::move(pitchTensor));
                } else {
                    ITask::setState(ITask::Failed);
                    return exp.takeError().withContext(R"(failed to build the "pitch" input)");
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
                    ITask::setState(ITask::Failed);
                    return exp.takeError().withContext(R"(failed to build the "retake" input)");
                }
                satisfyPitch = true;
            } else if (!satisfyExpr && isExpr) {
                if (auto exp = Tensor::create(ITensor::Float, {1, targetLength}); exp) {
                    auto exprTensor = exp.take();
                    if (exprTensor->elementCount() != targetLength) {
                        ITask::setState(ITask::Failed);
                        return srt::Error(ds::ErrorCode::ShapeMismatch,
                                          "expr tensor element count does not match target length");
                    }
                    auto exprBuffer = exprTensor->data<float>();
                    if (!exprBuffer) {
                        ITask::setState(ITask::Failed);
                        return srt::Error(ds::ErrorCode::ProcessingFailed,
                                          "failed to create expr tensor");
                    }
                    for (size_t i = 0; i < targetLength; ++i) {
                        exprBuffer[i] = static_cast<float>(samples[i]);
                    }
                    sessionInput->inputs.emplace("expr", std::move(exprTensor));
                    satisfyExpr = true;
                } else {
                    ITask::setState(ITask::Failed);
                    return exp.takeError().withContext(R"(failed to build the "expr" input)");
                }
            }
        }

        if (!satisfyPitch) {
            // No pitch supplied.
            // Will pass pitch tensor of all zeros and retake tensor of all true values.
            if (auto exp = Tensor::createFilled<float>({1, targetLength}, 0.0f); exp) {
                sessionInput->inputs.emplace("pitch", exp.take());
            } else {
                ITask::setState(ITask::Failed);
                return exp.takeError().withContext(R"(failed to build the "pitch" input)");
            }
            if (auto exp = Tensor::createFromRawData(ITensor::Bool, {1, targetLength},
                                                     Tensor::Container(targetLength, std::byte{1}));
                exp) {
                sessionInput->inputs.emplace("retake", exp.take());
                satisfyPitch = true;
            } else {
                ITask::setState(ITask::Failed);
                return exp.takeError().withContext(R"(failed to build the "retake" input)");
            }
        }

        if (!satisfyExpr) {
            // Model needs expr but no expr supplied.
            // Will use all ones instead.
            if (auto exp = Tensor::createFilled<float>({1, targetLength}, 1.0f); exp) {
                sessionInput->inputs.emplace("expr", exp.take());
                satisfyExpr = true;
            } else {
                ITask::setState(ITask::Failed);
                return exp.takeError().withContext(R"(failed to build the "expr" input)");
            }
        }

        // Speaker embedding
        if (config->useSpeakerEmbedding) {
            if (pitchInput.speakers.empty()) {
                ITask::setState(ITask::Failed);
                return srt::Error(ds::ErrorCode::InvalidInput, "no speakers found in pitch input");
            }

            auto exp = inferutil::preprocessSpeakerEmbeddingFrames(
                pitchInput.speakers, config->speakers, config->hiddenSize, frameWidth,
                targetLength);
            if (exp) {
                sessionInput->inputs["spk_embed"] = exp.take();
            } else {
                ITask::setState(ITask::Failed);
                return exp.takeError().withContext(R"(failed to build the "spk_embed" input)");
            }
        }

        // Select the model's acceleration representation.
        int64_t acceleration = pitchInput.steps;
        if (!config->useContinuousAcceleration) {
            acceleration = inferutil::getSpeedupFromSteps(acceleration);
        }
        {
            auto exp = Tensor::createScalar<int64_t>(acceleration);
            if (!exp) {
                ITask::setState(ITask::Failed);
                return exp.takeError().withContext(
                    stdc::formatN(R"(failed to build the "%1" input)",
                                  config->useContinuousAcceleration ? "steps" : "speedup"));
            }
            if (config->useContinuousAcceleration) {
                sessionInput->inputs["steps"] = exp.take();
            } else {
                sessionInput->inputs["speedup"] = exp.take();
            }
        }

        constexpr const char *outParamPitchPred = "pitch_pred";
        sessionInput->outputs.emplace(outParamPitchPred);

        std::unique_lock<std::shared_mutex> lock(m_mutex);
        if (!m_predictorSession || !m_predictorSession->isOpen()) {
            ITask::setState(ITask::Failed);
            return srt::Error(ds::ErrorCode::NotInitialized,
                              "pitch predictor session is not initialized");
        }

        std::unique_ptr<srt::TaskResult> sessionTaskResult;
        auto sessionExp = m_predictorSession->start(*sessionInput);
        if (!sessionExp) {
            ITask::setState(ITask::Failed);
            return sessionExp.takeError();
        } else {
            sessionTaskResult = sessionExp.take();
        }

        auto pitchResult = std::make_unique<Pit::PitchResult>();

        // Get session results
        if (!sessionTaskResult) {
            ITask::setState(ITask::Failed);
            return srt::Error(ds::ErrorCode::SessionFailed,
                              "pitch predictor session result is nullptr");
        }
        if (sessionTaskResult->type() != Onnx::API_NAME) {
            ITask::setState(ITask::Failed);
            return srt::Error(srt::Error::InvalidArgument, "invalid result API name");
        }
        auto sessionResult = sessionTaskResult->as<Onnx::SessionResult>();
        if (auto predictionIt = sessionResult->outputs.find(outParamPitchPred);
            predictionIt != sessionResult->outputs.end()) {
            // Extract onnx model result and copy to pitch final result vector (float -> double)
            auto output = std::move(predictionIt->second);
            if (output->dataType() != ITensor::Float) {
                ITask::setState(ITask::Failed);
                return srt::Error(ds::ErrorCode::SessionFailed, "model output is not float");
            }
            const auto view = output->view<float>();
            if (view.empty()) {
                ITask::setState(ITask::Failed);
                return srt::Error(ds::ErrorCode::SessionFailed, "model output is empty");
            }
            pitchResult->interval = frameWidth;
            pitchResult->pitch.assign(view.begin(), view.end());
        } else {
            ITask::setState(ITask::Failed);
            return srt::Error(ds::ErrorCode::SessionFailed, "invalid result output");
        }
        ITask::setState(ITask::Idle);
        return std::unique_ptr<srt::TaskResult>(std::move(pitchResult));
    }

    srt::Expected<void> PitchTask::startAsync(std::shared_ptr<const srt::TaskStartInput>,
                                              AsyncCallback) {
        return srt::Error(srt::Error::NotImplemented);
    }

    srt::Expected<void> PitchTask::stop() {
        for (auto *session : {m_encoderSession.get(), m_predictorSession.get()}) {
            if (session) {
                if (auto result = session->stop(); !result) {
                    return result;
                }
            }
        }
        ITask::setState(ITask::Canceled);
        return {};
    }

    srt::Expected<void> PitchTask::waitForFinished() {
        for (auto *session : {m_encoderSession.get(), m_predictorSession.get()}) {
            if (session) {
                if (auto result = session->waitForFinished(); !result) {
                    return result;
                }
            }
        }
        return {};
    }

}
