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

#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>
#include <dsinfer/Api/Inferences/Variance/1/VarianceApiL1.h>
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
    namespace Var = Api::Variance::L1;
    namespace Onnx = Api::Onnx;
    namespace DiffSinger = Api::DiffSinger::L1;

    static inline srt::Expected<srt::NO<Var::VarianceConfiguration>>
        getConfig(const srt::InferenceSpec *spec) {

        const auto genericConfig = spec->configuration();
        if (!genericConfig) {
            return srt::Error(srt::Error::InvalidArgument, "variance configuration is nullptr");
        }
        if (!(genericConfig->className() == Var::API_CLASS &&
              genericConfig->objectName() == Var::API_NAME)) {
            return srt::Error(srt::Error::InvalidArgument, "invalid variance configuration");
        }
        return genericConfig.as<Var::VarianceConfiguration>();
    }

    static inline srt::Expected<srt::NO<Var::VarianceSchema>>
        getSchema(const srt::InferenceSpec *spec) {

        const auto genericSchema = spec->schema();
        if (!genericSchema) {
            return srt::Error(srt::Error::InvalidArgument, "variance schema is nullptr");
        }
        if (!(genericSchema->className() == Var::API_CLASS &&
              genericSchema->objectName() == Var::API_NAME)) {
            return srt::Error(srt::Error::InvalidArgument, "invalid variance schema");
        }
        return genericSchema.as<Var::VarianceSchema>();
    }

    class VarianceInference::Impl {
    public:
        srt::NO<Var::VarianceResult> result;
        srt::NO<InferenceDriver> driver;
        srt::NO<InferenceSession> encoderSession;
        srt::NO<InferenceSession> predictorSession;
        mutable std::shared_mutex mutex;
    };

    VarianceInference::VarianceInference(const srt::InferenceSpec *spec)
        : Inference(spec), _impl(std::make_unique<Impl>()) {
    }

    VarianceInference::~VarianceInference() = default;

    srt::Expected<void> VarianceInference::initialize(const srt::NO<srt::TaskInitArgs> &args) {
        __stdc_impl_t;
        // Currently, no args to process. But we still need to enforce callers to pass the correct
        // args type.
        if (!args) {
            return srt::Error(srt::Error::InvalidArgument, "variance task init args is nullptr");
        }
        if (auto name = args->objectName(); name != Var::API_NAME) {
            return srt::Error(
                srt::Error::InvalidArgument,
                stdc::formatN(R"(invalid variance task init args name: expected "%1", got "%2")",
                              Var::API_NAME, name));
        }
        auto varianceArgs = args.as<Var::VarianceInitArgs>();

        std::unique_lock<std::shared_mutex> lock(impl.mutex);

        // If there are existing result, they will be cleared.
        impl.result.reset();

        if (auto res = inferutil::getInferenceDriver(this); res) {
            impl.driver = res.take();
        } else {
            setState(Failed);
            return res.takeError();
        }

        // Get variance config
        auto expConfig = getConfig(spec());
        if (!expConfig) {
            setState(Failed);
            return expConfig.takeError();
        }
        const auto config = expConfig.take();

        // Open variance session (encoder)
        impl.encoderSession = impl.driver->createSession();
        auto encoderOpenArgs = srt::NO<Onnx::SessionOpenArgs>::create();
        encoderOpenArgs->useCpu = false;
        if (auto res = impl.encoderSession->open(config->encoder, encoderOpenArgs); !res) {
            setState(Failed);
            return res;
        }

        // Open variance session (predictor)
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

    srt::Expected<srt::NO<srt::TaskResult>> VarianceInference::start(const srt::NO<srt::TaskStartInput> &input) {
        __stdc_impl_t;

        {
            std::shared_lock<std::shared_mutex> lock(impl.mutex);
            if (!impl.driver) {
                setState(Failed);
                return srt::Error(srt::Error::SessionError, "inference driver not initialized");
            }
        }

        setState(Running);

        // Get variance config
        auto expConfig = getConfig(spec());
        if (!expConfig) {
            setState(Failed);
            return expConfig.takeError();
        }
        const auto config = expConfig.take();

        // Get variance schema
        auto expSchema = getSchema(spec());
        if (!expSchema) {
            setState(Failed);
            return expSchema.takeError();
        }
        const auto schema = expSchema.take();

        if (!input) {
            setState(Failed);
            return srt::Error(srt::Error::InvalidArgument, "variance input is nullptr");
        }

        if (const auto &name = input->objectName(); name != Var::API_NAME) {
            setState(Failed);
            return srt::Error(
                srt::Error::InvalidArgument,
                stdc::formatN(R"(invalid variance task init args name: expected "%1", got "%2")",
                              Var::API_NAME, name));
        }

        const auto varianceInput = input.as<Var::VarianceStartInput>();
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
                            varianceInput->words, config->phonemes, config->languages,
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
                            varianceInput->words, config->phonemes, config->languages,
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
                                  "variance linguistic encoder session is not initialized");
            }
            if (auto encoderSessionExp =
                    inferutil::runEncoder(impl.encoderSession, linguisticInput,
                                                  /* out */ sessionInput, false);
                !encoderSessionExp) {
                setState(Failed);
                return encoderSessionExp.takeError();
            }
        }

        // Part 2: Variance Inference

        double totalDuration = 0.0;
        for (const auto &word : varianceInput->words) {
            totalDuration += inferutil::getWordDuration(word);
        }
        const auto targetLength = static_cast<int64_t>(std::llround(totalDuration / frameWidth));

        // ph_dur
        if (auto exp = inferutil::preprocessPhonemeDurations(varianceInput->words,
                                                                     config->frameWidth);
            exp) {
            sessionInput->inputs.emplace("ph_dur", exp.take());
        } else {
            setState(Failed);
            return exp.takeError();
        }

        // pitch and parameters
        if (schema->predictions.empty()) {
            setState(Failed);
            return srt::Error(srt::Error::SessionError, "no parameters to predict");
        }
        bool satisfyPitch = false;
        std::vector<bool> satisfyParams(schema->predictions.size(), false);

        constexpr auto kRetakeTrue = std::byte{1};
        constexpr auto kRetakeFalse = std::byte{0};
        Tensor::Container retake(targetLength * schema->predictions.size(), kRetakeTrue);

        for (const auto &param : varianceInput->parameters) {
            const auto isPitch = param.tag == Co::Tags::Pitch;

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
                    satisfyPitch = true;
                    continue;
                } else {
                    setState(Failed);
                    return exp.takeError();
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
                        setState(Failed);
                        return srt::Error(
                            srt::Error::SessionError,
                            "param tensor element count does not match target length");
                    }
                    auto paramBuffer = paramTensor->mutableData<float>();
                    if (!paramBuffer) {
                        setState(Failed);
                        return srt::Error(srt::Error::SessionError,
                                          "failed to create param tensor");
                    }
                    for (size_t i = 0; i < targetLength; ++i) {
                        paramBuffer[i] = static_cast<float>(samples[i]);
                    }
                    sessionInput->inputs.emplace(param.tag.name(), std::move(paramTensor));
                    sessionInput->outputs.emplace(std::string(param.tag.name()) + "_pred");
                } else {
                    setState(Failed);
                    return exp.takeError();
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
            setState(Failed);
            return exp.takeError();
        }

        if (!satisfyPitch) {
            setState(Failed);
            return srt::Error(srt::Error::SessionError, "missing pitch input");
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
                setState(Failed);
                return exp.takeError();
            }
        }

        // Speaker embedding
        if (config->useSpeakerEmbedding) {
            if (varianceInput->speakers.empty()) {
                setState(Failed);
                return srt::Error(srt::Error::SessionError, "no speakers found in variance input");
            }

            auto exp = inferutil::preprocessSpeakerEmbeddingFrames(
                varianceInput->speakers, config->speakers, config->hiddenSize, frameWidth,
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
        int64_t acceleration = varianceInput->steps;
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

        std::unique_lock<std::shared_mutex> lock(impl.mutex);
        if (!impl.predictorSession || !impl.predictorSession->isOpen()) {
            setState(Failed);
            return srt::Error(srt::Error::SessionError,
                              "variance predictor session is not initialized");
        }

        srt::NO<srt::TaskResult> sessionTaskResult;
        auto sessionExp = impl.predictorSession->start(sessionInput);
        if (!sessionExp) {
            setState(Failed);
            return sessionExp.takeError();
        } else {
            sessionTaskResult = sessionExp.take();
        }

        auto varianceResult = srt::NO<Var::VarianceResult>::create();

        // Get session results
        if (!sessionTaskResult) {
            setState(Failed);
            return srt::Error(srt::Error::SessionError,
                              "variance predictor session result is nullptr");
        }
        if (sessionTaskResult->objectName() != Onnx::API_NAME) {
            setState(Failed);
            return srt::Error(srt::Error::InvalidArgument, "invalid result API name");
        }
        auto sessionResult = sessionTaskResult.as<Onnx::SessionResult>();
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
            setState(Failed);
            return srt::Error(
                srt::Error::SessionError,
                stdc::formatN("predicted parameter count mismatch: expected %1, got %2",
                              expectedCount, actualCount));
        }
        impl.result = varianceResult;

        setState(Idle);
        return varianceResult;
    }

    srt::Expected<void> VarianceInference::startAsync(const srt::NO<srt::TaskStartInput> &input,
                                                   const StartAsyncCallback &callback) {
        // TODO:
        return srt::Error(srt::Error::NotImplemented);
    }

    bool VarianceInference::stop() {
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

    srt::NO<srt::TaskResult> VarianceInference::result() const {
        __stdc_impl_t;
        std::shared_lock<std::shared_mutex> lock(impl.mutex);
        return impl.result;
    }

}