#include "VarianceInference.h"

#include <cmath>
#include <cstring>
#include <mutex>
#include <numeric>
#include <shared_mutex>
#include <utility>

#include <stdcorelib/pimpl.h>
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

    class VarianceInference::Impl {
    public:
        InferenceDriver *driver = nullptr;
        std::unique_ptr<InferenceSession> encoderSession;
        std::unique_ptr<InferenceSession> predictorSession;
        mutable std::shared_mutex mutex;
    };

    VarianceInference::VarianceInference(srt::InferenceSpec &spec)
        : Inference(spec), _impl(std::make_unique<Impl>()) {
    }

    VarianceInference::~VarianceInference() = default;

    srt::Expected<void> VarianceInference::initialize(const srt::TaskInitArgs &args) {
        stdc_impl_t;
        // Currently, no args to process. But we still need to enforce callers to pass the correct
        // args type.
        if (auto name = args.type(); name != Var::API_INTERFACE) {
            return srt::Error(
                srt::Error::InvalidArgument,
                stdc::formatN(R"(invalid variance task init args name: expected "%1", got "%2")",
                              Var::API_INTERFACE, name));
        }
        const auto &varianceArgs = *args.as<Var::VarianceInitArgs>();

        std::unique_lock<std::shared_mutex> lock(impl.mutex);

        if (auto res = inferutil::getInferenceDriver(this); res) {
            impl.driver = res.take();
        } else {
            ITask::setState(ITask::Failed);
            return res.takeError();
        }

        // Get variance config
        auto expConfig = getConfig(spec());
        if (!expConfig) {
            ITask::setState(ITask::Failed);
            return expConfig.takeError();
        }
        const auto config = expConfig.take();

        // Open variance session (encoder)
        impl.encoderSession = impl.driver->createSession();
        Onnx::SessionOpenArgs encoderOpenArgs;
        encoderOpenArgs.useCpu = false;
        if (auto res = impl.encoderSession->open(config->encoder, encoderOpenArgs); !res) {
            ITask::setState(ITask::Failed);
            return res;
        }

        // Open variance session (predictor)
        impl.predictorSession = impl.driver->createSession();
        Onnx::SessionOpenArgs predictorOpenArgs;
        predictorOpenArgs.useCpu = false;
        if (auto res = impl.predictorSession->open(config->predictor, predictorOpenArgs); !res) {
            ITask::setState(ITask::Failed);
            return res;
        }

        // Initialize inference state
        ITask::setState(ITask::Idle);

        // return success
        return srt::Expected<void>();
    }

    srt::Expected<std::unique_ptr<srt::TaskResult>>
        VarianceInference::start(const srt::TaskStartInput &input) {
        stdc_impl_t;

        {
            std::shared_lock<std::shared_mutex> lock(impl.mutex);
            if (!impl.driver) {
                ITask::setState(ITask::Failed);
                return srt::Error(ds::ErrorCode::NotInitialized,
                                  "inference driver not initialized");
            }
        }

        ITask::setState(ITask::Running);

        // Get variance config
        auto expConfig = getConfig(spec());
        if (!expConfig) {
            ITask::setState(ITask::Failed);
            return expConfig.takeError();
        }
        const auto config = expConfig.take();

        // Get variance schema
        auto expSchema = getSchema(spec());
        if (!expSchema) {
            ITask::setState(ITask::Failed);
            return expSchema.takeError();
        }
        const auto schema = expSchema.take();


        if (const auto &name = input.type(); name != Var::API_INTERFACE) {
            ITask::setState(ITask::Failed);
            return srt::Error(
                srt::Error::InvalidArgument,
                stdc::formatN(R"(invalid variance task init args name: expected "%1", got "%2")",
                              Var::API_INTERFACE, name));
        }

        const auto &varianceInput = *input.as<Var::VarianceStartInput>();
        // ...

        auto sessionInput = std::make_shared<Onnx::SessionStartInput>();

        double frameWidth = config->frameWidth;
        if (!std::isfinite(frameWidth) || frameWidth <= 0) {
            ITask::setState(ITask::Failed);
            return srt::Error(srt::Error::InvalidArgument, "frame width must be positive");
        }

        // Part 1: Linguistic Encoder Inference
        {
            std::shared_ptr<Onnx::SessionStartInput> linguisticInput;
            switch (config->linguisticMode) {
                case Co::LinguisticMode::LM_Word:
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
                case Co::LinguisticMode::LM_Phoneme:
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
            std::unique_lock<std::shared_mutex> lock(impl.mutex);
            if (!impl.encoderSession || !impl.encoderSession->isOpen()) {
                ITask::setState(ITask::Failed);
                return srt::Error(ds::ErrorCode::NotInitialized,
                                  "variance linguistic encoder session is not initialized");
            }
            if (auto encoderSessionExp =
                    inferutil::runEncoder(impl.encoderSession.get(), *linguisticInput,
                                          /* out */ sessionInput, false);
                !encoderSessionExp) {
                ITask::setState(ITask::Failed);
                return encoderSessionExp.takeError().withContext("the linguistic encoder failed");
            }
        }

        // Part 2: Variance Inference

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
                    /// Note: startIndex is inclusive, endIndex is exclusive
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
                    auto it_begin = retake.begin() + startIndex;
                    auto it_end = retake.begin() + endIndex;

                    if (retakeStartFrame == retakeEndFrame) {
                        // Zero-length retake interval: mark entire region as 'no retake' (false)
                        std::fill(it_begin, it_end, kRetakeFalse);
                    } else if (retakeStartFrame < retakeEndFrame) {
                        // Mark frames before retake start as "no retake" (false)
                        std::fill(it_begin, it_begin + retakeStartFrame, kRetakeFalse);
                        // Frames in [retake start, retake end) remain true
                        // Mark frames after retake end as "no retake" (false)
                        std::fill(it_begin + retakeEndFrame, it_end, kRetakeFalse);
                    }
                } else {
                    // No retake specified: keep full region as true.
                    // Nothing to do here.
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
        } else {
            // Nothing to do: speaker embedding is not supported
        }

        // input param: steps / speedup
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

        std::unique_lock<std::shared_mutex> lock(impl.mutex);
        if (!impl.predictorSession || !impl.predictorSession->isOpen()) {
            ITask::setState(ITask::Failed);
            return srt::Error(ds::ErrorCode::NotInitialized,
                              "variance predictor session is not initialized");
        }

        std::unique_ptr<srt::TaskResult> sessionTaskResult;
        auto sessionExp = impl.predictorSession->start(*sessionInput);
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
        ITask::setState(ITask::Idle);
        return std::unique_ptr<srt::TaskResult>(std::move(varianceResult));
    }

    srt::Expected<void> VarianceInference::startAsync(std::shared_ptr<const srt::TaskStartInput>,
                                                      AsyncCallback) {
        // TODO:
        return srt::Error(srt::Error::NotImplemented);
    }

    srt::Expected<void> VarianceInference::stop() {
        stdc_impl_t;
        for (auto *session : {impl.encoderSession.get(), impl.predictorSession.get()}) {
            if (session) {
                if (auto result = session->stop(); !result) {
                    return result;
                }
            }
        }
        ITask::setState(ITask::Canceled);
        return {};
    }

    srt::Expected<void> VarianceInference::waitForFinished() {
        stdc_impl_t;
        for (auto *session : {impl.encoderSession.get(), impl.predictorSession.get()}) {
            if (session) {
                if (auto result = session->waitForFinished(); !result) {
                    return result;
                }
            }
        }
        return {};
    }

}
