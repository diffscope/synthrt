#include "VarianceInference.h"

#include <cmath>
#include <cstring>
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
#include <dsinfer/Api/Inferences/Variance/1/VarianceApiL1.h>
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
    namespace Var = Api::Variance::L1;
    namespace Onnx = srt::driver::onnx;
    namespace DiffSinger = Api::DiffSinger::L1;

    static srt::LogCategory Log("diffsinger.variance");

    static inline srt::core::Expected<srt::core::NO<Var::VarianceConfiguration>>
        getConfig(const srt::svs::InferenceSpec *spec) {

        const auto genericConfig = spec->configuration();
        if (!genericConfig) {
            return srt::core::Error(srt::core::ErrorCode::InvalidArgument,
                              "[Variance] configuration is nullptr");
        }
        if (!(genericConfig->className() == Var::API_CLASS &&
              genericConfig->objectName() == Var::API_NAME)) {
            return srt::core::Error(srt::core::ErrorCode::InvalidArgument,
                              "[Variance] invalid configuration");
        }
        return genericConfig.as<Var::VarianceConfiguration>();
    }

    static inline srt::core::Expected<srt::core::NO<Var::VarianceSchema>>
        getSchema(const srt::svs::InferenceSpec *spec) {

        const auto genericSchema = spec->schema();
        if (!genericSchema) {
            return srt::core::Error(srt::core::ErrorCode::InvalidArgument,
                              "[Variance] schema is nullptr");
        }
        if (!(genericSchema->className() == Var::API_CLASS &&
              genericSchema->objectName() == Var::API_NAME)) {
            return srt::core::Error(srt::core::ErrorCode::InvalidArgument,
                              "[Variance] invalid schema");
        }
        return genericSchema.as<Var::VarianceSchema>();
    }

    class VarianceInference::Impl {
    public:
        srt::core::NO<Var::VarianceResult> result;
        srt::core::NO<srt::driver::InferenceDriver> driver;
        srt::core::NO<srt::driver::InferenceSession> encoderSession;
        srt::core::NO<srt::driver::InferenceSession> predictorSession;
        mutable std::shared_mutex mutex;
    };

    VarianceInference::VarianceInference(const srt::svs::InferenceSpec *spec)
        : Inference(spec), _impl(std::make_unique<Impl>()) {
    }

    VarianceInference::~VarianceInference() = default;

    srt::core::Expected<void> VarianceInference::initialize(const srt::core::NO<srt::core::TaskInitArgs> &args) {
        __stdc_impl_t;
        // Currently, no args to process. But we still need to enforce callers to pass the correct
        // args type.
        if (!args) {
            return srt::core::Error(srt::core::ErrorCode::InvalidArgument,
                              "[Variance] task init args is nullptr");
        }
        if (auto name = args->objectName(); name != Var::API_NAME) {
            return srt::core::Error(
                srt::core::ErrorCode::InvalidArgument,
                stdc::formatN(R"([Variance] invalid task init args name: expected "%1", got "%2")",
                              Var::API_NAME, name));
        }
        auto varianceArgs = args.as<Var::VarianceInitArgs>();

        std::unique_lock<std::shared_mutex> lock(impl.mutex);

        // If there are existing result, they will be cleared.
        impl.result.reset();

        if (auto res = ds::infer::inferutil::getInferenceDriver(this); res) {
            impl.driver = res.take();
        } else {
            setState(Failed);
            Log.srtCritical("[Variance] initialize: failed to get inference driver: %1",
                            res.error().message());
            return res.takeError();
        }

        // Get variance config
        auto expConfig = getConfig(spec());
        if (!expConfig) {
            setState(Failed);
            Log.srtCritical("[Variance] initialize: %1", expConfig.error().message());
            return expConfig.takeError();
        }
        const auto config = expConfig.take();

        // Open variance session (encoder)
        impl.encoderSession = impl.driver->createSession();
        auto encoderOpenArgs = srt::core::NO<Onnx::SessionOpenArgs>::create();
        encoderOpenArgs->useCpu = false;
        if (auto res = impl.encoderSession->open(config->encoder, encoderOpenArgs); !res) {
            setState(Failed);
            Log.srtCritical("[Variance] initialize: failed to open encoder session for model %1",
                            stdc::path::to_utf8(config->encoder));
            return res;
        }

        // Open variance session (predictor)
        impl.predictorSession = impl.driver->createSession();
        auto predictorOpenArgs = srt::core::NO<Onnx::SessionOpenArgs>::create();
        predictorOpenArgs->useCpu = false;
        if (auto res = impl.predictorSession->open(config->predictor, predictorOpenArgs); !res) {
            setState(Failed);
            Log.srtCritical("[Variance] initialize: failed to open predictor session for model %1",
                            stdc::path::to_utf8(config->predictor));
            return res;
        }

        // Initialize inference state
        setState(Idle);

        // return success
        return srt::core::Expected<void>();
    }

    srt::core::Expected<srt::core::NO<srt::core::TaskResult>> VarianceInference::start(const srt::core::NO<srt::core::TaskStartInput> &input) {
        __stdc_impl_t;

        {
            std::shared_lock<std::shared_mutex> lock(impl.mutex);
            if (!impl.driver) {
                setState(Failed);
                Log.srtCritical("[Variance] start: inference driver not initialized");
                return srt::core::Error(srt::core::ErrorCode::InferenceStartFailed,
                                  "[Variance] inference driver not initialized");
            }
        }

        setState(Running);

        // Get variance config
        auto expConfig = getConfig(spec());
        if (!expConfig) {
            setState(Failed);
            Log.srtCritical("[Variance] start: %1", expConfig.error().message());
            return expConfig.takeError();
        }
        const auto config = expConfig.take();

        // Get variance schema
        auto expSchema = getSchema(spec());
        if (!expSchema) {
            setState(Failed);
            Log.srtCritical("[Variance] start: %1", expSchema.error().message());
            return expSchema.takeError();
        }
        const auto schema = expSchema.take();

        if (!input) {
            setState(Failed);
            Log.srtCritical("[Variance] start: input is nullptr");
            return srt::core::Error(srt::core::ErrorCode::InvalidArgument,
                              "[Variance] input is nullptr");
        }

        if (const auto &name = input->objectName(); name != Var::API_NAME) {
            setState(Failed);
            Log.srtCritical("[Variance] start: invalid input name: expected %1, got %2",
                            Var::API_NAME, name);
            return srt::core::Error(
                srt::core::ErrorCode::InvalidArgument,
                stdc::formatN(R"([Variance] invalid input name: expected "%1", got "%2")",
                              Var::API_NAME, name));
        }

        const auto varianceInput = input.as<Var::VarianceStartInput>();
        // ...

        auto sessionInput = srt::core::NO<Onnx::SessionStartInput>::create();

        double frameWidth = config->frameWidth;
        if (!std::isfinite(frameWidth) || frameWidth <= 0) {
            setState(Failed);
            Log.srtCritical("[Variance] start: frame width must be positive");
            return srt::core::Error(srt::core::ErrorCode::InvalidArgument,
                              "[Variance] frame width must be positive");
        }

        // Part 1: Linguistic Encoder Inference
        {
            // Auto-detect linguistic mode from ONNX model input names
            auto linguisticMode = config->linguisticMode;
            {
                const auto &names = impl.encoderSession->inputNames();
                bool hasWordDiv = std::find(names.begin(), names.end(), "word_div") != names.end();
                bool hasPhDur = std::find(names.begin(), names.end(), "ph_dur") != names.end();
                if (hasWordDiv && linguisticMode != Co::LinguisticMode::LM_Word) {
                    Log.srtWarning("[Variance] start: model expects word mode, overriding");
                    linguisticMode = Co::LinguisticMode::LM_Word;
                } else if (!hasWordDiv && hasPhDur && linguisticMode != Co::LinguisticMode::LM_Phoneme) {
                    Log.srtWarning("[Variance] start: model expects phoneme mode, overriding");
                    linguisticMode = Co::LinguisticMode::LM_Phoneme;
                }
            }
            srt::core::NO<Onnx::SessionStartInput> linguisticInput;
            switch (linguisticMode) {
                case Co::LinguisticMode::LM_Word:
                    if (auto exp = ds::infer::inferutil::preprocessLinguisticWord(
                            varianceInput->words, config->phonemes, config->languages,
                            config->useLanguageId, frameWidth);
                        exp) {
                        linguisticInput = exp.take();
                    } else {
                        setState(Failed);
                        Log.srtCritical("[Variance] start: preprocessLinguisticWord failed: %1",
                                        exp.error().message());
                        return exp.takeError();
                    }
                    break;
                case Co::LinguisticMode::LM_Phoneme:
                    if (auto exp = ds::infer::inferutil::preprocessLinguisticPhoneme(
                            varianceInput->words, config->phonemes, config->languages,
                            config->useLanguageId, frameWidth);
                        exp) {
                        linguisticInput = exp.take();
                    } else {
                        setState(Failed);
                        Log.srtCritical("[Variance] start: preprocessLinguisticPhoneme failed: %1",
                                        exp.error().message());
                        return exp.takeError();
                    }
                    break;
                default:
                    setState(Failed);
                    Log.srtCritical("[Variance] start: invalid LinguisticMode");
                    return srt::core::Error(srt::core::ErrorCode::InferenceInputInvalid,
                                      "[Variance] invalid LinguisticMode");
            }

            // Run Linguistic Encoder Inference
            std::unique_lock<std::shared_mutex> lock(impl.mutex);
            if (!impl.encoderSession || !impl.encoderSession->isOpen()) {
                setState(Failed);
                Log.srtCritical("[Variance] start: linguistic encoder session is not initialized");
                return srt::core::Error(srt::core::ErrorCode::InferenceStartFailed,
                                  "[Variance] linguistic encoder session is not initialized");
            }
            if (auto encoderSessionExp =
                    ds::infer::inferutil::runEncoder(impl.encoderSession, linguisticInput,
                                                  /* out */ sessionInput, false);
                !encoderSessionExp) {
                setState(Failed);
                Log.srtCritical("[Variance] start: runEncoder failed: %1",
                                encoderSessionExp.error().message());
                return encoderSessionExp.takeError();
            }
        }

        // Part 2: Variance Inference

        double totalDuration = 0.0;
        for (const auto &word : varianceInput->words) {
            totalDuration += ds::infer::inferutil::getWordDuration(word);
        }
        const auto targetLength = static_cast<int64_t>(std::llround(totalDuration / frameWidth));

        // ph_dur
        if (auto exp = ds::infer::inferutil::preprocessPhonemeDurations(varianceInput->words,
                                                                     config->frameWidth);
            exp) {
            sessionInput->inputs.emplace("ph_dur", exp.take());
        } else {
            setState(Failed);
            Log.srtCritical("[Variance] start: preprocessPhonemeDurations failed: %1",
                            exp.error().message());
            return exp.takeError();
        }

        // pitch and parameters
        if (schema->predictions.empty()) {
            setState(Failed);
            Log.srtCritical("[Variance] start: no parameters to predict");
            return srt::core::Error(srt::core::ErrorCode::InferenceInputInvalid,
                              "[Variance] no parameters to predict");
        }
        bool satisfyPitch = false;
        std::vector<bool> satisfyParams(schema->predictions.size(), false);

        constexpr auto kRetakeTrue = std::byte{1};
        constexpr auto kRetakeFalse = std::byte{0};
        srt::core::Tensor::Container retake(targetLength * schema->predictions.size(), kRetakeTrue);

        for (const auto &param : varianceInput->parameters) {
            const auto isPitch = param.tag == Co::Tags::Pitch;

            // Resample
            auto samples = ds::infer::inferutil::resample(param.values, param.interval, frameWidth,
                                                       targetLength, true);
            if (samples.size() != targetLength) {
                setState(Failed);
                Log.srtCritical("[Variance] start: parameter %1 resample failed",
                                std::string(param.tag.name()));
                return srt::core::Error(srt::core::ErrorCode::InferenceInputInvalid, "[Variance] parameter " +
                                                                std::string(param.tag.name()) +
                                                                " resample failed");
            }

            if (isPitch) {
                if (auto exp = srt::core::Tensor::create(srt::core::ITensor::Float, {1, targetLength}); exp) {
                    auto pitchTensor = exp.take();
                    if (pitchTensor->elementCount() != targetLength) {
                        setState(Failed);
                        Log.srtCritical("[Variance] start: pitch tensor element count does not match target length");
                        return srt::core::Error(
                            srt::core::ErrorCode::InferenceTensorCreateFailed,
                            "[Variance] pitch tensor element count does not match target length");
                    }
                    auto pitchBuffer = pitchTensor->mutableData<float>();
                    if (!pitchBuffer) {
                        setState(Failed);
                        Log.srtCritical("[Variance] start: failed to create pitch tensor");
                        return srt::core::Error(srt::core::ErrorCode::InferenceTensorCreateFailed,
                                          "[Variance] failed to create pitch tensor");
                    }
                    for (size_t i = 0; i < targetLength; ++i) {
                        pitchBuffer[i] = static_cast<float>(samples[i]);
                    }
                    sessionInput->inputs.emplace("pitch", std::move(pitchTensor));
                    satisfyPitch = true;
                    continue;
                } else {
                    setState(Failed);
                    Log.srtCritical("[Variance] start: failed to create pitch tensor: %1",
                                    exp.error().message());
                    return exp.takeError();
                }
            }

            for (size_t j = 0; j < schema->predictions.size(); ++j) {
                const auto &prediction = schema->predictions[j];
                if (param.tag != prediction) {
                    continue;
                }
                if (auto exp = srt::core::Tensor::create(srt::core::ITensor::Float, {1, targetLength}); exp) {
                    auto paramTensor = exp.take();
                    if (paramTensor->elementCount() != targetLength) {
                        setState(Failed);
                        Log.srtCritical("[Variance] start: param tensor element count does not match target length");
                        return srt::core::Error(
                            srt::core::ErrorCode::InferenceTensorCreateFailed,
                            "[Variance] param tensor element count does not match target length");
                    }
                    auto paramBuffer = paramTensor->mutableData<float>();
                    if (!paramBuffer) {
                        setState(Failed);
                        Log.srtCritical("[Variance] start: failed to create param tensor");
                        return srt::core::Error(srt::core::ErrorCode::InferenceTensorCreateFailed,
                                          "[Variance] failed to create param tensor");
                    }
                    for (size_t i = 0; i < targetLength; ++i) {
                        paramBuffer[i] = static_cast<float>(samples[i]);
                    }
                    sessionInput->inputs.emplace(param.tag.name(), std::move(paramTensor));
                    sessionInput->outputs.emplace(std::string(param.tag.name()) + "_pred");
                } else {
                    setState(Failed);
                    Log.srtCritical("[Variance] start: failed to create param tensor: %1",
                                    exp.error().message());
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

        if (auto exp = srt::core::Tensor::createFromRawData(
                srt::core::ITensor::Bool, {1, targetLength, static_cast<int64_t>(schema->predictions.size())},
                std::move(retake));
            exp) {
            sessionInput->inputs.emplace("retake", exp.take());
        } else {
            setState(Failed);
            Log.srtCritical("[Variance] start: failed to create retake tensor: %1",
                            exp.error().message());
            return exp.takeError();
        }

        if (!satisfyPitch) {
            setState(Failed);
            Log.srtCritical("[Variance] start: missing pitch input");
            return srt::core::Error(srt::core::ErrorCode::InferenceInputInvalid,
                              "[Variance] missing pitch input");
        }

        for (size_t j = 0; j < schema->predictions.size(); ++j) {
            if (satisfyParams[j]) {
                continue;
            }
            const auto &prediction = schema->predictions[j];
            // If some parameters are not supplied, fill them with 0
            auto exp = srt::core::Tensor::createFilled<float>({1, targetLength}, 0.0f);
            if (exp) {
                sessionInput->inputs.emplace(prediction.name(), exp.take());
                sessionInput->outputs.emplace(std::string(prediction.name()) + "_pred");
            } else {
                setState(Failed);
                Log.srtCritical("[Variance] start: failed to create fallback tensor for parameter %1: %2",
                                std::string(prediction.name()), exp.error().message());
                return exp.takeError();
            }
        }

        // Speaker embedding
        if (config->useSpeakerEmbedding) {
            if (varianceInput->speakers.empty()) {
                setState(Failed);
                Log.srtCritical("[Variance] start: no speakers found in input");
                return srt::core::Error(srt::core::ErrorCode::InferenceSpeakerNotFound,
                                  "[Variance] no speakers found in input");
            }

            auto exp = ds::infer::inferutil::preprocessSpeakerEmbeddingFrames(
                varianceInput->speakers, config->speakers, config->hiddenSize, frameWidth,
                targetLength);
            if (exp) {
                sessionInput->inputs["spk_embed"] = exp.take();
            } else {
                setState(Failed);
                Log.srtCritical("[Variance] start: preprocessSpeakerEmbeddingFrames failed: %1",
                                exp.error().message());
                return exp.takeError();
            }
        } else {
            // Nothing to do: speaker embedding is not supported
        }

        // input param: steps / speedup
        int64_t acceleration = varianceInput->steps;
        if (!config->useContinuousAcceleration) {
            acceleration = ds::infer::inferutil::getSpeedupFromSteps(acceleration);
        }
        {
            auto exp = srt::core::Tensor::createScalar<int64_t>(acceleration);
            if (!exp) {
                setState(Failed);
                Log.srtCritical("[Variance] start: failed to create steps/speedup tensor: %1",
                                exp.error().message());
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
            Log.srtCritical("[Variance] start: predictor session is not initialized");
            return srt::core::Error(srt::core::ErrorCode::InferenceStartFailed,
                              "[Variance] predictor session is not initialized");
        }

        srt::core::NO<srt::core::TaskResult> sessionTaskResult;
        auto sessionExp = impl.predictorSession->start(sessionInput);
        if (!sessionExp) {
            setState(Failed);
            Log.srtCritical("[Variance] start: predictor session->start failed: %1",
                            sessionExp.error().message());
            return sessionExp.takeError();
        } else {
            sessionTaskResult = sessionExp.take();
        }

        auto varianceResult = srt::core::NO<Var::VarianceResult>::create();

        // Get session results
        if (!sessionTaskResult) {
            setState(Failed);
            Log.srtCritical("[Variance] start: predictor session result is nullptr");
            return srt::core::Error(srt::core::ErrorCode::InferenceOutputEmpty,
                              "[Variance] predictor session result is nullptr");
        }
        if (sessionTaskResult->objectName() != Onnx::API_NAME) {
            setState(Failed);
            Log.srtCritical("[Variance] start: invalid result API name: %1",
                            sessionTaskResult->objectName());
            return srt::core::Error(srt::core::ErrorCode::InvalidArgument,
                              "[Variance] invalid result API name");
        }
        auto sessionResult = sessionTaskResult.as<Onnx::SessionResult>();
        varianceResult->predictions.reserve(sessionResult->outputs.size());
        for (const auto &[outputName, output] : sessionResult->outputs) {
            for (const auto &prediction : schema->predictions) {
                if (outputName != std::string(prediction.name()) + "_pred") {
                    continue;
                }
                const auto view = output->view<float>();
                if (view.empty() && output->elementCount() > 0) {
                    setState(Failed);
                    Log.srtCritical("[Variance] start: model output is not float");
                    return srt::core::Error(srt::core::ErrorCode::InferenceDataTypeMismatch,
                                      "[Variance] model output is not float");
                }
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
            Log.srtCritical("[Variance] start: predicted parameter count mismatch: expected %1, got %2",
                            expectedCount, actualCount);
            return srt::core::Error(
                srt::core::ErrorCode::InferenceRunFailed,
                stdc::formatN("[Variance] predicted parameter count mismatch: expected %1, got %2",
                              expectedCount, actualCount));
        }
        impl.result = varianceResult;

        setState(Idle);
        return varianceResult;
    }

    srt::core::Expected<void> VarianceInference::startAsync(const srt::core::NO<srt::core::TaskStartInput> &input,
                                                   const StartAsyncCallback &callback) {
        // TODO:
        return srt::core::Error(srt::core::ErrorCode::NotImplemented, "not implemented");
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

    srt::core::NO<srt::core::TaskResult> VarianceInference::result() const {
        __stdc_impl_t;
        std::shared_lock<std::shared_mutex> lock(impl.mutex);
        return impl.result;
    }

}