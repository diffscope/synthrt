#include "VarianceTask.h"

#include <algorithm>
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
#include <dsinfer/Api/Inferences/Variance/1/VarianceOnnxApiL1.h>
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

    static inline srt::Expected<const Var::Onnx::Configuration *>
        getConfig(const srt::InferenceSpec &spec) {

        const auto genericConfig = spec.configuration();
        if (!genericConfig) {
            return srt::Error(srt::Error::InvalidArgument, "variance configuration is nullptr");
        }
        if (genericConfig->triplet() != Var::Onnx::Triplet) {
            return srt::Error(srt::Error::InvalidArgument, "invalid variance configuration");
        }
        return static_cast<const Var::Onnx::Configuration *>(genericConfig);
    }

    static inline srt::Expected<const Var::Exports *> getExports(const srt::InferenceSpec &spec) {

        const auto genericSchema = spec.exports();
        if (!genericSchema) {
            return srt::Error(srt::Error::InvalidArgument, "variance schema is nullptr");
        }
        if (genericSchema->contract() != Var::Contract) {
            return srt::Error(srt::Error::InvalidArgument, "invalid variance schema");
        }
        return static_cast<const Var::Exports *>(genericSchema);
    }

    static inline srt::Expected<std::vector<ParamTag>>
        selectPredictions(const Var::Exports &schema, const std::set<ParamTag> &selected) {
        if (schema.predictions.empty()) {
            return srt::Error(ds::ErrorCode::InvalidInput, "no parameters to predict");
        }
        if (selected.empty()) {
            return schema.predictions;
        }

        std::vector<ParamTag> result;
        for (const auto &prediction : schema.predictions) {
            if (selected.find(prediction) != selected.end()) {
                result.push_back(prediction);
            }
        }
        if (result.empty()) {
            return srt::Error(ds::ErrorCode::InvalidInput,
                              "the import options select no exported parameter");
        }
        for (const auto &prediction : selected) {
            if (std::find(schema.predictions.begin(), schema.predictions.end(), prediction) ==
                schema.predictions.end()) {
                return srt::Error(ds::ErrorCode::InvalidInput,
                                  "the import options select an unknown parameter: " +
                                      std::string(prediction.name()));
            }
        }
        return result;
    }

    VarianceTask::VarianceTask(VarianceInference &inference) : VarianceTask(inference, {}) {
    }

    VarianceTask::VarianceTask(VarianceInference &inference, const Var::ImportOptions &options)
        : ITask(Var::Contract.interfaceId(), Var::Contract.level()), m_inference(&inference) {
        m_speakerMapping = options.speakerMapping;
        m_predictions = options.predictions;
    }

    VarianceTask::~VarianceTask() {
        (void) stop();
        (void) waitForFinished();
    }

    srt::Expected<void> VarianceTask::initialize(const Var::InitArgs &args) {
        return initialize(static_cast<const srt::TaskInitArgs &>(args));
    }

    srt::Expected<std::unique_ptr<Var::Result>> VarianceTask::start(const Var::StartInput &input) {
        auto result = ITask::start(input);
        if (!result) {
            return result.takeError();
        }
        return std::unique_ptr<Var::Result>(static_cast<Var::Result *>(result.take().release()));
    }

    srt::Expected<void> VarianceTask::startAsync(std::shared_ptr<const Var::StartInput> input,
                                                 Var::Executive::AsyncCallback callback) {
        if (!callback) {
            return srt::Error(srt::Error::InvalidArgument,
                              "variance asynchronous callback must not be empty");
        }
        auto genericInput = std::static_pointer_cast<const srt::TaskStartInput>(std::move(input));
        auto started =
            ITask::startAsync(std::move(genericInput),
                              [callback = std::move(callback)](
                                  srt::Expected<std::unique_ptr<srt::TaskResult>> result) mutable {
                                  if (!result) {
                                      callback(result.takeError());
                                      return;
                                  }
                                  callback(std::unique_ptr<Var::Result>(
                                      static_cast<Var::Result *>(result.take().release())));
                              });
        if (!started) {
            return started.takeError();
        }
        return {};
    }

    srt::Expected<void> VarianceTask::initialize(const srt::TaskInitArgs &args) {
        if (args.type() != Var::Contract.interfaceId() || args.version() != Var::Contract.level()) {
            return srt::Error(
                srt::Error::InvalidArgument,
                stdc::formatN(
                    R"(invalid variance initialization payload: expected "%1" level %2, got "%3" level %4)",
                    Var::Contract.interfaceId(), Var::Contract.level(), args.type(),
                    args.version()));
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
        VarianceTask::execute(const srt::TaskStartInput &input, const srt::TaskStopToken &) {

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
        auto expExports = getExports(m_inference->spec());
        if (!expExports) {
            ITask::setState(ITask::Failed);
            return expExports.takeError();
        }
        const auto schema = expExports.take();
        auto expPredictions = selectPredictions(*schema, m_predictions);
        if (!expPredictions) {
            ITask::setState(ITask::Failed);
            return expPredictions.takeError();
        }
        const auto predictions = expPredictions.take();
        const auto &modelPredictions = schema->predictions;


        if (input.type() != Var::Contract.interfaceId() ||
            input.version() != Var::Contract.level()) {
            ITask::setState(ITask::Failed);
            return srt::Error(
                srt::Error::InvalidArgument,
                stdc::formatN(
                    R"(invalid variance start payload: expected "%1" level %2, got "%3" level %4)",
                    Var::Contract.interfaceId(), Var::Contract.level(), input.type(),
                    input.version()));
        }

        const auto &varianceInput = *input.as<Var::StartInput>();

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
        if (static_cast<uint64_t>(targetLength) > std::numeric_limits<size_t>::max()) {
            ITask::setState(ITask::Failed);
            return srt::Error(ds::ErrorCode::InvalidInput,
                              "variance frame count exceeds the addressable range");
        }
        const auto frameCount = static_cast<size_t>(targetLength);
        const auto predictionCount = modelPredictions.size();
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
            if (!isPitch && std::find(modelPredictions.begin(), modelPredictions.end(),
                                      param.tag) == modelPredictions.end()) {
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
                    satisfyPitch = true;
                    continue;
                } else {
                    ITask::setState(ITask::Failed);
                    return exp.takeError().withContext(R"(failed to build the "pitch" input)");
                }
            }

            for (size_t j = 0; j < modelPredictions.size(); ++j) {
                const auto &prediction = modelPredictions[j];
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
                    if (std::find(predictions.begin(), predictions.end(), param.tag) !=
                        predictions.end()) {
                        sessionInput->outputs.emplace(std::string(param.tag.name()) + "_pred");
                    }
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

        for (size_t j = 0; j < modelPredictions.size(); ++j) {
            if (satisfyParams[j]) {
                continue;
            }
            const auto &prediction = modelPredictions[j];
            // If some parameters are not supplied, fill them with 0
            auto exp = Tensor::createFilled<float>({1, targetLength}, 0.0f);
            if (exp) {
                sessionInput->inputs.emplace(prediction.name(), exp.take());
                if (std::find(predictions.begin(), predictions.end(), prediction) !=
                    predictions.end()) {
                    sessionInput->outputs.emplace(std::string(prediction.name()) + "_pred");
                }
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
                varianceInput.speakers, m_speakerMapping, config->speakers, config->hiddenSize,
                frameWidth, targetLength);
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

        auto varianceResult = std::make_unique<Var::Result>();

        // Get session results
        if (!sessionTaskResult) {
            ITask::setState(ITask::Failed);
            return srt::Error(ds::ErrorCode::SessionFailed,
                              "variance predictor session result is nullptr");
        }
        if (sessionTaskResult->type() != Onnx::Name ||
            sessionTaskResult->version() != Onnx::Version) {
            ITask::setState(ITask::Failed);
            return srt::Error(srt::Error::InvalidArgument, "invalid session result contract");
        }
        auto sessionResult = sessionTaskResult->as<Onnx::SessionResult>();
        varianceResult->predictions.reserve(predictions.size());
        const std::vector<int64_t> expectedShape = {1, targetLength};
        for (const auto &prediction : predictions) {
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

    srt::Expected<void> VarianceTask::quit() {
        srt::Error stopError;
        for (auto session : {m_encoderSession.get(), m_predictorSession.get()}) {
            if (session && session->state() == Running) {
                if (auto result = session->stop(); !result && stopError.ok()) {
                    stopError = result.takeError();
                }
            }
        }
        if (!stopError.ok()) {
            return stopError;
        }
        return {};
    }

    srt::Expected<void> VarianceTask::wait() {
        srt::Error waitError;
        for (auto session : {m_encoderSession.get(), m_predictorSession.get()}) {
            if (session) {
                if (auto result = session->waitForFinished(); !result && waitError.ok()) {
                    waitError = result.takeError();
                }
            }
        }
        if (!waitError.ok()) {
            return waitError;
        }
        return {};
    }

    VarianceTask::State VarianceTask::classifyOutcome(
        const srt::Expected<std::unique_ptr<srt::TaskResult>> &outcome,
        bool stopRequested) noexcept {
        if (!outcome && stopRequested) {
            return Canceled;
        }
        return outcome ? Succeeded : Failed;
    }

}
