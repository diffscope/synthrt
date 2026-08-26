#include "VarianceTask.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
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
        const auto frameCountValue = totalDuration / frameWidth;
        if (!std::isfinite(frameCountValue) || frameCountValue <= 0 ||
            frameCountValue >= static_cast<double>(std::numeric_limits<int64_t>::max())) {
            ITask::setState(ITask::Failed);
            return srt::Error(ds::ErrorCode::InvalidInput,
                              "variance input produces an invalid frame count");
        }
        const auto targetLength = static_cast<int64_t>(std::llround(frameCountValue));
        if (targetLength <= 0) {
            ITask::setState(ITask::Failed);
            return srt::Error(ds::ErrorCode::InvalidInput,
                              "variance input must contain at least one frame");
        }

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
        if (static_cast<uint64_t>(targetLength) > std::numeric_limits<size_t>::max()) {
            ITask::setState(ITask::Failed);
            return srt::Error(ds::ErrorCode::InvalidInput,
                              "variance frame count exceeds the addressable range");
        }
        const auto frameCount = static_cast<size_t>(targetLength);
        const auto predictionCount = schema->predictions.size();
        if (predictionCount > static_cast<size_t>(std::numeric_limits<int64_t>::max()) ||
            predictionCount > std::numeric_limits<size_t>::max() / frameCount) {
            ITask::setState(ITask::Failed);
            return srt::Error(ds::ErrorCode::InvalidInput,
                              "variance retake tensor dimensions are too large");
        }
        bool satisfyPitch = false;
        std::vector<bool> satisfyParams(predictionCount, false);

        constexpr auto kRetakeTrue = std::byte{1};
        constexpr auto kRetakeFalse = std::byte{0};
        Tensor::Container retake(frameCount * predictionCount, kRetakeTrue);

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

                    const auto toFrame = [&](double time, int64_t fallback) {
                        if (!std::isfinite(time) || time < 0) {
                            return fallback;
                        }
                        const auto frame = time / frameWidth;
                        if (frame <= 0) {
                            return int64_t{0};
                        }
                        if (frame >= static_cast<double>(targetLength)) {
                            return targetLength;
                        }
                        return static_cast<int64_t>(std::llround(frame));
                    };
                    const auto retakeStartFrame = toFrame(start, 0);
                    const auto retakeEndFrame = toFrame(end, targetLength);
                    if (retakeStartFrame > retakeEndFrame) {
                        ITask::setState(ITask::Failed);
                        return srt::Error(ds::ErrorCode::InvalidInput,
                                          "variance retake start must not exceed its end");
                    }

                    // The tensor shape is (frame, prediction), so one prediction occupies a
                    // strided column rather than a contiguous range.
                    for (size_t i = 0; i < frameCount; ++i) {
                        const auto inRange = static_cast<int64_t>(i) >= retakeStartFrame &&
                                             static_cast<int64_t>(i) < retakeEndFrame;
                        retake[i * predictionCount + j] = inRange ? kRetakeTrue : kRetakeFalse;
                    }
                }
                satisfyParams[j] = true;
            }
        }

        if (auto exp = Tensor::createFromRawData(
                ITensor::Bool, {1, targetLength, static_cast<int64_t>(predictionCount)},
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
        if (sessionTaskResult->type() != Onnx::API_NAME ||
            sessionTaskResult->version() != Onnx::API_VERSION) {
            ITask::setState(ITask::Failed);
            return srt::Error(srt::Error::InvalidArgument, "invalid session result contract");
        }
        auto sessionResult = sessionTaskResult->as<Onnx::SessionResult>();
        varianceResult->predictions.reserve(predictionCount);
        const std::vector<int64_t> expectedShape = {1, targetLength};
        for (const auto &prediction : schema->predictions) {
            const auto outputName = std::string(prediction.name()) + "_pred";
            const auto outputIt = sessionResult->outputs.find(outputName);
            if (outputIt == sessionResult->outputs.end() || !outputIt->second) {
                ITask::setState(ITask::Failed);
                return srt::Error(ds::ErrorCode::SessionFailed,
                                  "variance result is missing output " + outputName);
            }
            const auto &output = outputIt->second;
            if (output->dataType() != ITensor::Float) {
                ITask::setState(ITask::Failed);
                return srt::Error(ds::ErrorCode::SessionFailed,
                                  "variance output " + outputName + " is not float");
            }
            if (output->shape() != expectedShape || output->elementCount() != frameCount) {
                ITask::setState(ITask::Failed);
                return srt::Error(ds::ErrorCode::ShapeMismatch,
                                  "variance output " + outputName +
                                      " does not have shape (1, n_frames)");
            }
            const auto view = output->view<float>();
            if (view.size() != frameCount) {
                ITask::setState(ITask::Failed);
                return srt::Error(ds::ErrorCode::ShapeMismatch,
                                  "variance output " + outputName +
                                      " has an invalid element count");
            }
            Co::InputParameterInfo inputParam{prediction};
            inputParam.interval = frameWidth;
            inputParam.values.assign(view.begin(), view.end());
            varianceResult->predictions.emplace_back(std::move(inputParam));
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
