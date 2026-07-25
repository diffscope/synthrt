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
#include <inferutil/PluginCommon.h>

namespace srt::svs {

    namespace Co = Api::Common::L1;
    namespace Var = Api::Variance::L1;
    namespace Onnx = srt::driver::onnx;
    namespace DiffSinger = Api::DiffSinger::L1;

    static constexpr auto kLogPrefix = "[Variance]";

    static srt::LogCategory Log("diffsinger.variance");

    class VarianceInference::Impl {
    public:
        srt::core::NO<Var::VarianceResult> result;
        srt::core::NO<srt::driver::InferenceDriver> driver;
        srt::core::NO<srt::driver::InferenceSession> encoderSession;
        srt::core::NO<srt::driver::InferenceSession> predictorSession;
        mutable std::shared_mutex mutex;
    };

    VarianceInference::VarianceInference(const srt::svs::InferenceSpec *spec)
        : Inference(spec), m_impl(std::make_unique<Impl>()) {
    }

    VarianceInference::~VarianceInference() = default;

    srt::core::Expected<void> VarianceInference::initialize(const srt::core::NO<srt::core::TaskInitArgs> &args) {
        auto &impl = *m_impl;
        // Currently, no args to process. But we still need to enforce callers to pass the correct
        // args type.
        if (auto res = ds::infer::inferutil::validateInitArgs(args, Var::API_NAME, kLogPrefix); !res) {
            setState(Failed);
            Log.srtCritical("%1 initialize: %2", kLogPrefix, res.error().message());
            return res.takeError();
        }
        auto varianceArgs = args.as<Var::VarianceInitArgs>();
        if (!varianceArgs) {
            setState(Failed);
            Log.srtCritical("%1 initialize: type mismatch, expected VarianceInitArgs", kLogPrefix);
            return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceInputInvalid,
                              "[Variance] type mismatch, expected VarianceInitArgs",
                              {}, "variance");
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

        // Get variance config
        auto expConfig = ds::infer::inferutil::getTypedConfig<Var::VarianceConfiguration>(spec(), Var::API_CLASS, Var::API_NAME, kLogPrefix);
        if (!expConfig) {
            setState(Failed);
            Log.srtCritical("%1 initialize: %2", kLogPrefix, expConfig.error().message());
            return expConfig.takeError();
        }
        const auto config = expConfig.take();

        // Open variance session (encoder)
        if (auto exp = ds::infer::inferutil::openOnnxSession(
                impl.driver, config->encoder, false, "encoder", kLogPrefix);
            exp) {
            impl.encoderSession = exp.take();
        } else {
            setState(Failed);
            Log.srtCritical("%1", exp.error().message());
            return exp.takeError();
        }

        // Open variance session (predictor)
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

    srt::core::Expected<srt::core::NO<srt::core::TaskResult>> VarianceInference::start(const srt::core::NO<srt::core::TaskStartInput> &input) {
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

        // Get variance config
        auto expConfig = ds::infer::inferutil::getTypedConfig<Var::VarianceConfiguration>(spec(), Var::API_CLASS, Var::API_NAME, kLogPrefix);
        if (!expConfig) {
            setState(Failed);
            Log.srtCritical("%1 start: %2", kLogPrefix, expConfig.error().message());
            return expConfig.takeError();
        }
        const auto config = expConfig.take();

        // Get variance schema
        auto expSchema = ds::infer::inferutil::getTypedSchema<Var::VarianceSchema>(spec(), Var::API_CLASS, Var::API_NAME, kLogPrefix);
        if (!expSchema) {
            setState(Failed);
            Log.srtCritical("%1 start: %2", kLogPrefix, expSchema.error().message());
            return expSchema.takeError();
        }
        const auto schema = expSchema.take();

        if (auto res = ds::infer::inferutil::validateStartInput(input, Var::API_NAME, kLogPrefix); !res) {
            setState(Failed);
            Log.srtCritical("%1", res.error().message());
            return res.takeError();
        }

        const auto varianceInput = input.as<Var::VarianceStartInput>();
        if (!varianceInput) {
            setState(Failed);
            Log.srtCritical("%1 start: type mismatch, expected VarianceStartInput", kLogPrefix);
            return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceInputInvalid,
                              "[Variance] type mismatch, expected VarianceStartInput",
                              {}, "variance");
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
                // BUG-PLUGIN-VAR-01: Protect encoderSession access with a shared
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
                            varianceInput->words, config->phonemes, config->languages,
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
                            varianceInput->words, config->phonemes, config->languages,
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
                                      "[Variance] invalid LinguisticMode",
                                      {}, "variance");
            }

            // Run Linguistic Encoder Inference
            std::unique_lock<std::shared_mutex> lock(impl.mutex);
            if (!impl.encoderSession || !impl.encoderSession->isOpen()) {
                setState(Failed);
                Log.srtCritical("%1 start: linguistic encoder session is not initialized", kLogPrefix);
                return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceStartFailed,
                                  "[Variance] linguistic encoder session is not initialized",
                                  {}, "variance");
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
            Log.srtCritical("%1 start: preprocessPhonemeDurations failed: %2",
                            kLogPrefix, exp.error().message());
            return exp.takeError();
        }

        // pitch and parameters
        if (schema->predictions.empty()) {
            setState(Failed);
            Log.srtCritical("%1 start: no parameters to predict", kLogPrefix);
            return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceInputInvalid,
                              "[Variance] no parameters to predict",
                              {}, "variance");
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
                Log.srtCritical("%1 start: parameter %2 resample failed",
                                kLogPrefix, std::string(param.tag.name()));
                return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceInputInvalid, "[Variance] parameter " +
                                                                std::string(param.tag.name()) +
                                                                " resample failed",
                                                                {}, "variance");
            }

            if (isPitch) {
                if (auto exp = srt::core::Tensor::create(srt::core::ITensor::Float, {1, targetLength}); exp) {
                    auto pitchTensor = exp.take();
                    if (pitchTensor->elementCount() != targetLength) {
                        setState(Failed);
                        Log.srtCritical("%1 start: pitch tensor element count does not match target length", kLogPrefix);
                        return srt::core::Error::inferenceError(
                            srt::core::ErrorCode::InferenceTensorCreateFailed,
                            "[Variance] pitch tensor element count does not match target length",
                            {}, "variance");
                    }
                    auto pitchBuffer = pitchTensor->mutableData<float>();
                    if (!pitchBuffer) {
                        setState(Failed);
                        Log.srtCritical("%1 start: failed to create pitch tensor", kLogPrefix);
                        return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceTensorCreateFailed,
                                          "[Variance] failed to create pitch tensor",
                                          {}, "variance");
                    }
                    for (size_t i = 0; i < targetLength; ++i) {
                        pitchBuffer[i] = static_cast<float>(samples[i]);
                    }
                    sessionInput->inputs.emplace("pitch", std::move(pitchTensor));
                    satisfyPitch = true;
                    continue;
                } else {
                    setState(Failed);
                    Log.srtCritical("%1 start: failed to create pitch tensor: %2",
                                    kLogPrefix, exp.error().message());
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
                        Log.srtCritical("%1 start: param tensor element count does not match target length", kLogPrefix);
                        return srt::core::Error::inferenceError(
                            srt::core::ErrorCode::InferenceTensorCreateFailed,
                            "[Variance] param tensor element count does not match target length",
                            {}, "variance");
                    }
                    auto paramBuffer = paramTensor->mutableData<float>();
                    if (!paramBuffer) {
                        setState(Failed);
                        Log.srtCritical("%1 start: failed to create param tensor", kLogPrefix);
                        return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceTensorCreateFailed,
                                          "[Variance] failed to create param tensor",
                                          {}, "variance");
                    }
                    for (size_t i = 0; i < targetLength; ++i) {
                        paramBuffer[i] = static_cast<float>(samples[i]);
                    }
                    sessionInput->inputs.emplace(param.tag.name(), std::move(paramTensor));
                    sessionInput->outputs.emplace(std::string(param.tag.name()) + "_pred");
                } else {
                    setState(Failed);
                    Log.srtCritical("%1 start: failed to create param tensor: %2",
                                    kLogPrefix, exp.error().message());
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
            Log.srtCritical("%1 start: failed to create retake tensor: %2",
                            kLogPrefix, exp.error().message());
            return exp.takeError();
        }

        if (!satisfyPitch) {
            setState(Failed);
            Log.srtCritical("%1 start: missing pitch input", kLogPrefix);
            return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceInputInvalid,
                              "[Variance] missing pitch input",
                              {}, "variance");
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
                Log.srtCritical("%1 start: failed to create fallback tensor for parameter %2: %3",
                                kLogPrefix, std::string(prediction.name()), exp.error().message());
                return exp.takeError();
            }
        }

        // Speaker embedding
        if (config->useSpeakerEmbedding) {
            if (varianceInput->speakers.empty()) {
                setState(Failed);
                Log.srtCritical("%1 start: no speakers found in input", kLogPrefix);
                return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceSpeakerNotFound,
                                  "[Variance] no speakers found in input",
                                  {}, "variance");
            }

            auto exp = ds::infer::inferutil::preprocessSpeakerEmbeddingFrames(
                varianceInput->speakers, config->speakers, config->hiddenSize, frameWidth,
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
        int64_t acceleration = varianceInput->steps;
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

        std::unique_lock<std::shared_mutex> lock(impl.mutex);
        if (!impl.predictorSession || !impl.predictorSession->isOpen()) {
            setState(Failed);
            Log.srtCritical("%1 start: predictor session is not initialized", kLogPrefix);
            return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceStartFailed,
                              "[Variance] predictor session is not initialized",
                              {}, "variance");
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

        auto varianceResult = srt::core::NO<Var::VarianceResult>::create();

        // Get session results
        if (!sessionTaskResult) {
            setState(Failed);
            Log.srtCritical("%1 start: predictor session result is nullptr", kLogPrefix);
            return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceOutputEmpty,
                              "[Variance] predictor session result is nullptr",
                              {}, "variance");
        }
        if (sessionTaskResult->objectName() != Onnx::API_NAME) {
            setState(Failed);
            Log.srtCritical("%1 start: invalid result API name: %2",
                            kLogPrefix, sessionTaskResult->objectName());
            return srt::core::Error::inferenceError(srt::core::ErrorCode::InvalidArgument,
                              "[Variance] invalid result API name",
                              {}, "variance");
        }
        auto sessionResult = sessionTaskResult.as<Onnx::SessionResult>();
        if (!sessionResult) {
            setState(Failed);
            Log.srtCritical("%1 start: type mismatch, expected SessionResult", kLogPrefix);
            return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceRunFailed,
                              "[Variance] type mismatch, expected SessionResult",
                              {}, "variance");
        }
        varianceResult->predictions.reserve(sessionResult->outputs.size());
        for (const auto &[outputName, output] : sessionResult->outputs) {
            for (const auto &prediction : schema->predictions) {
                if (outputName != std::string(prediction.name()) + "_pred") {
                    continue;
                }
                if (output->dataType() != srt::core::ITensor::Float) {
                    setState(Failed);
                    Log.srtCritical("%1 start: model output is not float", kLogPrefix);
                    return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceDataTypeMismatch,
                                      "[Variance] model output is not float",
                                      {}, "variance");
                }
                const auto view = output->view<float>();
                if (view.empty()) {
                    setState(Failed);
                    Log.srtCritical("%1 start: model output is empty", kLogPrefix);
                    return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceOutputEmpty,
                                      "[Variance] model output is empty",
                                      {}, "variance");
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
            Log.srtCritical("%1 start: predicted parameter count mismatch: expected %2, got %3",
                            kLogPrefix, expectedCount, actualCount);
            return srt::core::Error::inferenceError(
                srt::core::ErrorCode::InferenceRunFailed,
                stdc::formatN("[Variance] predicted parameter count mismatch: expected %1, got %2",
                              expectedCount, actualCount),
                {}, "variance");
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

    srt::core::NO<srt::core::TaskResult> VarianceInference::result() const {
        auto &impl = *m_impl;
        std::shared_lock<std::shared_mutex> lock(impl.mutex);
        return impl.result;
    }

}