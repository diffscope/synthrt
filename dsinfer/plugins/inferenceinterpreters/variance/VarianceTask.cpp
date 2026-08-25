#include "VarianceTask.h"

#include <cmath>
#include <cstring>
#include <mutex>
#include <numeric>
#include <shared_mutex>
#include <tuple>
#include <utility>

#include <stdcorelib/str.h>
#include <stdcorelib/path.h>

#include <dsinfer/Support/ErrorCode.h>
#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>
#include <dsinfer/Api/Inferences/Variance/1/VarianceApiL1.h>
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

#include "VarianceInference.h"

namespace ds {

    namespace Co = Api::Common::L1;
    namespace Var = Api::Variance::L1;
    namespace Onnx = Api::Onnx;

    static inline srt::Expected<const Var::VarianceConfiguration *>
        getConfig(const srt::InferenceSpec &spec) {

        const auto genericConfig = spec.configuration();
        if (!genericConfig) {
            return srt::Error(srt::Error::InvalidArgument, "variance configuration is nullptr");
        }
        if (genericConfig->interface() != Var::API_INTERFACE ||
            genericConfig->variant() != Var::API_VARIANT ||
            genericConfig->level() != Var::API_LEVEL) {
            return srt::Error(srt::Error::InvalidArgument, "invalid variance configuration");
        }
        return static_cast<const Var::VarianceConfiguration *>(genericConfig);
    }

    static inline srt::Expected<const Var::VarianceSchema *>
        getSchema(const srt::InferenceSpec &spec) {

        const auto genericSchema = spec.exports();
        if (!genericSchema) {
            return srt::Error(srt::Error::InvalidArgument, "variance schema is nullptr");
        }
        if (genericSchema->interface() != Var::API_INTERFACE ||
            genericSchema->variant() != Var::API_VARIANT ||
            genericSchema->level() != Var::API_LEVEL) {
            return srt::Error(srt::Error::InvalidArgument, "invalid variance schema");
        }
        return static_cast<const Var::VarianceSchema *>(genericSchema);
    }

    VarianceTask::VarianceTask(VarianceInference &inference) : m_inference(&inference) {
    }

    VarianceTask::~VarianceTask() {
        if (state() == Running) {
            std::ignore = stop();
        }
        std::ignore = waitForFinished();
    }

    srt::Expected<void> VarianceTask::initialize(const Var::VarianceInitArgs &args) {
        return initialize(static_cast<const srt::TaskInitArgs &>(args));
    }

    srt::Expected<std::unique_ptr<Var::VarianceResult>>
        VarianceTask::start(const Var::VarianceStartInput &input) {
        auto result = start(static_cast<const srt::TaskStartInput &>(input));
        if (!result) {
            return result.takeError();
        }
        return std::unique_ptr<Var::VarianceResult>(
            static_cast<Var::VarianceResult *>(result.take().release()));
    }

    srt::Expected<void>
        VarianceTask::startAsync(std::shared_ptr<const Var::VarianceStartInput> input,
                                 Var::VarianceExecutive::AsyncCallback callback) {
        if (!callback) {
            return srt::Error(srt::Error::InvalidArgument,
                              "variance asynchronous callback must not be empty");
        }
        auto genericInput = std::static_pointer_cast<const srt::TaskStartInput>(std::move(input));
        return startAsync(std::move(genericInput),
                          [callback = std::move(callback)](
                              srt::Expected<std::unique_ptr<srt::TaskResult>> result) mutable {
                              if (!result) {
                                  callback(result.takeError());
                                  return;
                              }
                              callback(std::unique_ptr<Var::VarianceResult>(
                                  static_cast<Var::VarianceResult *>(result.take().release())));
                          });
    }

    srt::Expected<void> VarianceTask::initialize(const srt::TaskInitArgs &args) {
        if (args.type() != Var::API_INTERFACE || args.version() != Var::API_LEVEL) {
            return srt::Error(
                srt::Error::InvalidArgument,
                stdc::formatN(
                    R"(invalid variance initialization payload: expected "%1" level %2, got "%3" level %4)",
                    Var::API_INTERFACE, Var::API_LEVEL, args.type(), args.version()));
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

        // Open variance session (encoder)
        m_encoderSession = m_driver->createSession();
        Onnx::SessionOpenArgs encoderOpenArgs;
        encoderOpenArgs.useCpu = false;
        if (auto res = m_encoderSession->open(config->encoder, encoderOpenArgs); !res) {
            ITask::setState(ITask::Failed);
            return res;
        }

        // Open variance session (predictor)
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
        VarianceTask::start(const srt::TaskStartInput &input) {

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

        // Get variance schema
        auto expSchema = getSchema(m_inference->spec());
        if (!expSchema) {
            ITask::setState(ITask::Failed);
            return expSchema.takeError();
        }
        const auto schema = expSchema.take();


        if (input.type() != Var::API_INTERFACE || input.version() != Var::API_LEVEL) {
            ITask::setState(ITask::Failed);
            return srt::Error(
                srt::Error::InvalidArgument,
                stdc::formatN(
                    R"(invalid variance start payload: expected "%1" level %2, got "%3" level %4)",
                    Var::API_INTERFACE, Var::API_LEVEL, input.type(), input.version()));
        }

        const auto &varianceInput = *input.as<Var::VarianceStartInput>();

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
                            varianceInput.words, config->phonemes, config->languages,
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
                            varianceInput.words, config->phonemes, config->languages,
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
                                  "variance linguistic encoder session is not initialized");
            }
            if (auto encoderSessionExp =
                    inferutil::runEncoder(m_encoderSession.get(), *linguisticInput,
                                          /* out */ sessionInput, false);
                !encoderSessionExp) {
                ITask::setState(ITask::Failed);
                return encoderSessionExp.takeError().withContext("the linguistic encoder failed");
            }
        }

        // Prepare and run the variance predictor.

        double totalDuration = 0.0;
        for (const auto &word : varianceInput.words) {
            totalDuration += inferutil::getWordDuration(word);
        }
        const auto targetLength = static_cast<int64_t>(std::llround(totalDuration / frameWidth));

        // ph_dur
        if (auto exp =
                inferutil::preprocessPhonemeDurations(varianceInput.words, config->frameWidth);
            exp) {
            sessionInput->inputs.emplace("ph_dur", exp.take());
        } else {
            ITask::setState(ITask::Failed);
            return exp.takeError().withContext(R"(failed to build the "ph_dur" input)");
        }

        // pitch and parameters
        if (schema->predictions.empty()) {
            ITask::setState(ITask::Failed);
            return srt::Error(ds::ErrorCode::InvalidInput, "no parameters to predict");
        }
        bool satisfyPitch = false;
        std::vector<bool> satisfyParams(schema->predictions.size(), false);

        constexpr auto kRetakeTrue = std::byte{1};
        constexpr auto kRetakeFalse = std::byte{0};
        Tensor::Container retake(targetLength * schema->predictions.size(), kRetakeTrue);

        for (const auto &param : varianceInput.parameters) {
            const auto isPitch = param.tag == Co::Tags::Pitch;

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
                    satisfyPitch = true;
                    continue;
                } else {
                    ITask::setState(ITask::Failed);
                    return exp.takeError().withContext(R"(failed to build the "pitch" input)");
                }
            }

            for (size_t j = 0; j < schema->predictions.size(); ++j) {
                const auto &prediction = schema->predictions[j];
                if (param.tag != prediction) {
                    continue;
                }
                if (auto exp = Tensor::create(ITensor::Float, {1, targetLength}); exp) {
                    auto paramTensor = exp.take();
                    if (paramTensor->elementCount() != targetLength) {
                        ITask::setState(ITask::Failed);
                        return srt::Error(
                            ds::ErrorCode::ShapeMismatch,
                            "param tensor element count does not match target length");
                    }
                    auto paramBuffer = paramTensor->data<float>();
                    if (!paramBuffer) {
                        ITask::setState(ITask::Failed);
                        return srt::Error(ds::ErrorCode::ProcessingFailed,
                                          "failed to create param tensor");
                    }
                    for (size_t i = 0; i < targetLength; ++i) {
                        paramBuffer[i] = static_cast<float>(samples[i]);
                    }
                    sessionInput->inputs.emplace(param.tag.name(), std::move(paramTensor));
                    sessionInput->outputs.emplace(std::string(param.tag.name()) + "_pred");
                } else {
                    ITask::setState(ITask::Failed);
                    return exp.takeError().withContext(
                        stdc::formatN(R"(failed to build the "%1" input)", param.tag.name()));
                }

                // Retake
                if (param.retake.has_value()) {
                    const auto &[start, end] = *param.retake;

                    // Compute frame index range for this parameter
                    // startIndex is inclusive. endIndex is exclusive.
                    const auto startIndex = static_cast<int64_t>(j * targetLength);
                    const auto endIndex = static_cast<int64_t>((j + 1) * targetLength);

                    // Convert retake start/end time (in seconds) to frame indices,
                    // clamped to [0, targetLength]
                    // Note: retakeStartFrame is inclusive, retakeEndFrame is exclusive
                    int64_t retakeStartFrame = 0;
                    if (std::isfinite(start) && start >= 0) {
                        retakeStartFrame = std::clamp<int64_t>(
                            static_cast<int64_t>(std::llround(start / frameWidth)), int64_t{0},
                            targetLength);
                    } else {
                        // For invalid start (NaN, Inf, or negative): default to 0
                    }
                    int64_t retakeEndFrame = targetLength;
                    if (std::isfinite(end) && end >= 0) {
                        retakeEndFrame = std::clamp<int64_t>(
                            static_cast<int64_t>(std::llround(end / frameWidth)), int64_t{0},
                            targetLength);
                    } else {
                        // For invalid end (NaN, Inf, or negative): default to last frame
                    }

                    // Get iterators pointing to the beginning and end
                    // of this parameter's retake region in the tensor
                    auto beginIt = retake.begin() + startIndex;
                    auto endIt = retake.begin() + endIndex;

                    if (retakeStartFrame == retakeEndFrame) {
                        // Zero-length retake interval: mark entire region as 'no retake' (false)
                        std::fill(beginIt, endIt, kRetakeFalse);
                    } else if (retakeStartFrame < retakeEndFrame) {
                        // Mark frames before retake start as "no retake" (false)
                        std::fill(beginIt, beginIt + retakeStartFrame, kRetakeFalse);
                        // Frames in [retake start, retake end) remain true
                        // Mark frames after retake end as "no retake" (false)
                        std::fill(beginIt + retakeEndFrame, endIt, kRetakeFalse);
                    }
                }
                satisfyParams[j] = true;
            }
        }

        if (auto exp = Tensor::createFromRawData(
                ITensor::Bool, {1, targetLength, static_cast<int64_t>(schema->predictions.size())},
                std::move(retake));
            exp) {
            sessionInput->inputs.emplace("retake", exp.take());
        } else {
            ITask::setState(ITask::Failed);
            return exp.takeError().withContext(R"(failed to build the "retake" input)");
        }

        if (!satisfyPitch) {
            ITask::setState(ITask::Failed);
            return srt::Error(ds::ErrorCode::InvalidInput, "missing pitch input");
        }

        for (size_t j = 0; j < schema->predictions.size(); ++j) {
            if (satisfyParams[j]) {
                continue;
            }
            const auto &prediction = schema->predictions[j];
            // If some parameters are not supplied, fill them with 0
            auto exp = Tensor::createFilled<float>({1, targetLength}, 0.0f);
            if (exp) {
                sessionInput->inputs.emplace(prediction.name(), exp.take());
                sessionInput->outputs.emplace(std::string(prediction.name()) + "_pred");
            } else {
                ITask::setState(ITask::Failed);
                return exp.takeError().withContext(
                    stdc::formatN(R"(failed to build the "%1" input)", prediction.name()));
            }
        }

        // Speaker embedding
        if (config->useSpeakerEmbedding) {
            if (varianceInput.speakers.empty()) {
                ITask::setState(ITask::Failed);
                return srt::Error(ds::ErrorCode::InvalidInput,
                                  "no speakers found in variance input");
            }

            auto exp = inferutil::preprocessSpeakerEmbeddingFrames(
                varianceInput.speakers, config->speakers, config->hiddenSize, frameWidth,
                targetLength);
            if (exp) {
                sessionInput->inputs["spk_embed"] = exp.take();
            } else {
                ITask::setState(ITask::Failed);
                return exp.takeError().withContext(R"(failed to build the "spk_embed" input)");
            }
        }

        // Select the model's acceleration representation.
        int64_t acceleration = varianceInput.steps;
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

        std::unique_lock<std::shared_mutex> lock(m_mutex);
        if (!m_predictorSession || !m_predictorSession->isOpen()) {
            ITask::setState(ITask::Failed);
            return srt::Error(ds::ErrorCode::NotInitialized,
                              "variance predictor session is not initialized");
        }

        std::unique_ptr<srt::TaskResult> sessionTaskResult;
        auto sessionExp = m_predictorSession->start(*sessionInput);
        if (!sessionExp) {
            ITask::setState(ITask::Failed);
            return sessionExp.takeError();
        } else {
            sessionTaskResult = sessionExp.take();
        }

        auto varianceResult = std::make_unique<Var::VarianceResult>();

        // Get session results
        if (!sessionTaskResult) {
            ITask::setState(ITask::Failed);
            return srt::Error(ds::ErrorCode::SessionFailed,
                              "variance predictor session result is nullptr");
        }
        if (sessionTaskResult->type() != Onnx::API_NAME) {
            ITask::setState(ITask::Failed);
            return srt::Error(srt::Error::InvalidArgument, "invalid result API name");
        }
        auto sessionResult = sessionTaskResult->as<Onnx::SessionResult>();
        varianceResult->predictions.reserve(sessionResult->outputs.size());
        for (const auto &[outputName, output] : sessionResult->outputs) {
            for (const auto &prediction : schema->predictions) {
                if (outputName != std::string(prediction.name()) + "_pred") {
                    continue;
                }
                const auto view = output->view<float>();
                Co::InputParameterInfo inputParam{prediction};
                inputParam.interval = frameWidth;
                inputParam.values.assign(view.begin(), view.end());
                varianceResult->predictions.emplace_back(std::move(inputParam));
            }
        }

        const auto expectedCount = schema->predictions.size();
        const auto actualCount = varianceResult->predictions.size();
        if (expectedCount != actualCount) {
            ITask::setState(ITask::Failed);
            return srt::Error(
                ds::ErrorCode::ShapeMismatch,
                stdc::formatN("predicted parameter count mismatch: expected %1, got %2",
                              expectedCount, actualCount));
        }
        ITask::setState(ITask::Succeeded);
        return std::unique_ptr<srt::TaskResult>(std::move(varianceResult));
    }

    srt::Expected<void> VarianceTask::startAsync(std::shared_ptr<const srt::TaskStartInput> input,
                                                 AsyncCallback callback) {
        if (!input || input->type() != Var::API_INTERFACE || input->version() != Var::API_LEVEL) {
            return srt::Error(srt::Error::InvalidArgument,
                              "invalid variance asynchronous input payload");
        }
        return ITask::startAsync(std::move(input), std::move(callback));
    }

    srt::Expected<void> VarianceTask::stop() {
        requestAsyncCancellation();
        srt::Error stopError;
        for (auto session : {m_encoderSession.get(), m_predictorSession.get()}) {
            if (session && session->state() == Running) {
                if (auto result = session->stop(); !result && stopError.ok()) {
                    stopError = result.takeError();
                }
            }
        }
        ITask::setState(ITask::Canceled);
        if (!stopError.ok()) {
            return stopError;
        }
        return {};
    }

    srt::Expected<void> VarianceTask::waitForFinished() {
        srt::Error waitError;
        for (auto session : {m_encoderSession.get(), m_predictorSession.get()}) {
            if (session) {
                if (auto result = session->waitForFinished(); !result && waitError.ok()) {
                    waitError = result.takeError();
                }
            }
        }
        waitForAsyncExecution();
        if (!waitError.ok()) {
            return waitError;
        }
        return {};
    }

}
