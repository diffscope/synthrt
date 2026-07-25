#include "DurationInference.h"

#include <cmath>
#include <fstream>
#include <mutex>
#include <numeric>
#include <shared_mutex>
#include <utility>

#include <stdcorelib/pimpl.h>
#include <stdcorelib/str.h>
#include <stdcorelib/path.h>

#include <synthrt/Core/Support/Logging.h>

#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>
#include <dsinfer/Api/Inferences/Duration/1/DurationApiL1.h>
#include <synthrt/Driver/onnx/OnnxDriverApi.h>
#include <dsinfer/Api/Singers/DiffSinger/1/DiffSingerApiL1.h>
#include <synthrt/Driver/InferenceDriver.h>
#include <synthrt/Driver/InferenceSession.h>
#include <synthrt/Core/Tensor/ITensor.h>
#include <synthrt/Core/Tensor/Tensor.h>

#include <inferutil/Driver.h>
#include <inferutil/InputWord.h>
#include <inferutil/LinguisticEncoder.h>
#include <inferutil/Algorithm.h>
#include <inferutil/PluginCommon.h>

namespace srt::svs {

    namespace Co = Api::Common::L1;
    namespace Dur = Api::Duration::L1;
    namespace Onnx = srt::driver::onnx;
    namespace DiffSinger = Api::DiffSinger::L1;

    static constexpr auto kLogPrefix = "[Duration]";

    static srt::LogCategory Log("diffsinger.duration");

    static inline srt::core::Expected<srt::core::NO<srt::core::ITensor>>
        preprocessPhonemeMidi(const std::vector<Api::Common::L1::InputWordInfo> &words) {

        auto phoneCount = ds::infer::inferutil::getPhoneCount(words);

        std::vector<uint8_t> isRest;
        std::vector<int64_t> phMidi;
        isRest.reserve(phoneCount);
        phMidi.reserve(phoneCount);

        for (const auto &word : words) {
            if (word.notes.empty())
                continue;

            std::vector<double> cumDur;
            double s = 0;
            for (const auto &note : word.notes) {
                s += note.duration;
                cumDur.push_back(s);
            }

            for (const auto &phone : word.phones) {
                size_t idx = 0;
                while (idx < cumDur.size() && phone.start > cumDur[idx]) {
                    ++idx;
                }
                if (idx >= word.notes.size())
                    idx = word.notes.size() - 1;

                const auto &note = word.notes[idx];
                const auto rest = static_cast<uint8_t>(note.is_rest);
                isRest.push_back(rest);
                phMidi.push_back(rest ? 0 : note.key);
            }

            if (!ds::infer::inferutil::fillRestMidiWithNearestInPlace<int64_t>(phMidi, isRest)) {
                return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceInputInvalid,
                                  "[Duration] failed to fill rest notes",
                                  {}, "duration");
            }
        }

        std::vector<int64_t> shape{1, static_cast<int64_t>(phMidi.size())};
        if (auto exp = srt::core::Tensor::createFromView<int64_t>(shape, stdc::array_view<int64_t>{phMidi});
            exp) {
            return exp.take();
        } else {
            return exp.takeError();
        }
    }

    class DurationInference::Impl {
    public:
        srt::core::NO<Dur::DurationResult> result;
        srt::core::NO<srt::driver::InferenceDriver> driver;
        srt::core::NO<srt::driver::InferenceSession> encoderSession;
        srt::core::NO<srt::driver::InferenceSession> predictorSession;
        mutable std::shared_mutex mutex;
    };

    DurationInference::DurationInference(const srt::svs::InferenceSpec *spec)
        : Inference(spec), m_impl(std::make_unique<Impl>()) {
    }

    DurationInference::~DurationInference() = default;

    srt::core::Expected<void> DurationInference::initialize(const srt::core::NO<srt::core::TaskInitArgs> &args) {
        auto &impl = *m_impl;
        // Currently, no args to process. But we still need to enforce callers to pass the correct
        // args type.
        if (auto res = ds::infer::inferutil::validateInitArgs(args, Dur::API_NAME, kLogPrefix); !res) {
            setState(Failed);
            Log.srtCritical("%1 initialize: %2", kLogPrefix, res.error().message());
            return res.takeError();
        }
        auto durationArgs = args.as<Dur::DurationInitArgs>();
        if (!durationArgs) {
            setState(Failed);
            Log.srtCritical("%1 initialize: type mismatch, expected DurationInitArgs", kLogPrefix);
            return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceInputInvalid,
                              "[Duration] type mismatch, expected DurationInitArgs",
                              {}, "duration");
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

        // Get duration config
        auto expConfig = ds::infer::inferutil::getTypedConfig<Dur::DurationConfiguration>(spec(), Dur::API_CLASS, Dur::API_NAME, kLogPrefix);
        if (!expConfig) {
            setState(Failed);
            Log.srtCritical("%1 initialize: %2", kLogPrefix, expConfig.error().message());
            return expConfig.takeError();
        }
        const auto config = expConfig.take();

        // Open duration session (encoder)
        if (auto exp = ds::infer::inferutil::openOnnxSession(
                impl.driver, config->encoder, false, "encoder", kLogPrefix);
            exp) {
            impl.encoderSession = exp.take();
        } else {
            setState(Failed);
            Log.srtCritical("%1", exp.error().message());
            return exp.takeError();
        }

        // Open duration session (predictor)
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

    srt::core::Expected<srt::core::NO<srt::core::TaskResult>>
        DurationInference::start(const srt::core::NO<srt::core::TaskStartInput> &input) {

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

        // Get duration config
        auto expConfig = ds::infer::inferutil::getTypedConfig<Dur::DurationConfiguration>(spec(), Dur::API_CLASS, Dur::API_NAME, kLogPrefix);
        if (!expConfig) {
            setState(Failed);
            Log.srtCritical("%1 start: %2", kLogPrefix, expConfig.error().message());
            return expConfig.takeError();
        }
        const auto config = expConfig.take();

        if (auto res = ds::infer::inferutil::validateStartInput(input, Dur::API_NAME, kLogPrefix); !res) {
            setState(Failed);
            Log.srtCritical("%1", res.error().message());
            return res.takeError();
        }

        auto durationInput = input.as<Dur::DurationStartInput>();
        if (!durationInput) {
            setState(Failed);
            Log.srtCritical("%1 start: type mismatch, expected DurationStartInput", kLogPrefix);
            return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceInputInvalid,
                              "[Duration] type mismatch, expected DurationStartInput",
                              {}, "duration");
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
        if (auto exp = ds::infer::inferutil::preprocessLinguisticWord(
                durationInput->words, config->phonemes, config->languages, config->useLanguageId,
                frameWidth);
            exp) {
            // Run Linguistic Encoder Inference
            std::unique_lock<std::shared_mutex> lock(impl.mutex);
            if (!impl.encoderSession || !impl.encoderSession->isOpen()) {
                setState(Failed);
                Log.srtCritical("%1 start: linguistic encoder session is not initialized", kLogPrefix);
                return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceStartFailed,
                                  "[Duration] linguistic encoder session is not initialized",
                                  {}, "duration");
            }
            if (auto encoderSessionExp =
                    ds::infer::inferutil::runEncoder(impl.encoderSession, exp.take(),
                                                  /* out */ sessionInput);
                !encoderSessionExp) {
                setState(Failed);
                Log.srtCritical("%1 start: runEncoder failed: %2",
                                kLogPrefix, encoderSessionExp.error().message());
                return encoderSessionExp.takeError();
            }
        } else {
            setState(Failed);
            Log.srtCritical("%1 start: preprocessLinguisticWord failed: %2",
                            kLogPrefix, exp.error().message());
            return exp.takeError();
        }

        // Part 2: Duration Inference
        if (auto exp = preprocessPhonemeMidi(durationInput->words); exp) {
            sessionInput->inputs["ph_midi"] = exp.take();
        } else {
            setState(Failed);
            Log.srtCritical("%1 start: preprocessPhonemeMidi failed: %2",
                            kLogPrefix, exp.error().message());
            return exp.takeError();
        }

        auto phoneCount = ds::infer::inferutil::getPhoneCount(durationInput->words);
        if (config->useSpeakerEmbedding) {
            std::vector<int64_t> shape = {1, static_cast<int64_t>(phoneCount), config->hiddenSize};
            if (auto exp = srt::core::Tensor::create(srt::core::ITensor::Float, shape); exp) {
                // get tensor buffer
                auto tensor = exp.take();
                auto buffer = tensor->mutableData<float>();
                if (!buffer) {
                    setState(Failed);
                    Log.srtCritical("%1 start: failed to create spk_embed tensor", kLogPrefix);
                    return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceTensorCreateFailed,
                                      "[Duration] failed to create spk_embed tensor",
                                      {}, "duration");
                }

                // mix speaker embedding
                int currPhoneIndex = 0;
                for (const auto &word : durationInput->words) {
                    for (const auto &phone : word.phones) {
                        if (phone.speakers.empty()) {
                            setState(Failed);
                            Log.srtCritical("%1 start: phoneme %2 missing speakers",
                                            kLogPrefix, phone.token);
                            return srt::core::Error::inferenceError(
                                srt::core::ErrorCode::InferenceSpeakerNotFound,
                                stdc::formatN("[Duration] phoneme %1 missing speakers", phone.token),
                                {}, "duration");
                        }
                        for (const auto &speaker : phone.speakers) {
                            const std::vector<float> *embeddingPtr = nullptr;

                            // 1. Try voice bank lookup by name
                            if (auto it_speaker = config->speakers.find(speaker.name);
                                it_speaker != config->speakers.end()) {
                                embeddingPtr = &it_speaker->second;
                            }
                            // 2. Fall back to inline embedding (allows custom/undefined speakers)
                            else if (!speaker.embedding.empty()) {
                                embeddingPtr = &speaker.embedding;
                            } else {
                                // BF-37: Previously this was silently skipped
                                // (no else branch), leaving the phoneme's
                                // embedding as all-zeros. Now returns an error
                                // per ROBUST-05.
                                setState(Failed);
                                Log.srtCritical("%1 start: speaker %2 not found for "
                                                "phoneme %3 and no inline embedding provided",
                                                kLogPrefix, speaker.name, phone.token);
                                return srt::core::Error::inferenceError(
                                    srt::core::ErrorCode::InferenceSpeakerNotFound,
                                    stdc::formatN("[Duration] speaker %1 not found in voice bank "
                                                  "and no inline embedding provided (phoneme %2)",
                                                  speaker.name, phone.token),
                                    {}, "duration");
                            }

                            const auto &embedding = *embeddingPtr;
                            if (embedding.size() != static_cast<size_t>(config->hiddenSize)) {
                                setState(Failed);
                                Log.srtCritical("%1 start: speaker embedding vector length does not match hiddenSize", kLogPrefix);
                                return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceInputInvalid,
                                                  "[Duration] speaker embedding vector length does not "
                                                  "match hiddenSize",
                                                  {}, "duration");
                            }
                            for (size_t j = 0; j < embedding.size(); ++j) {
                                float &val = buffer[currPhoneIndex * embedding.size() + j];
                                val = std::fmaf(static_cast<float>(speaker.proportion),
                                                embedding[j], val);
                            }
                        }
                        ++currPhoneIndex;
                    }
                }
                sessionInput->inputs["spk_embed"] = tensor;
            } else {
                setState(Failed);
                Log.srtCritical("%1 start: failed to create spk_embed tensor shape: %2",
                                kLogPrefix, exp.error().message());
                return exp.takeError();
            }
        } else {
            // Nothing to do: speaker embedding is not supported
        }

        constexpr const char *outParamPhDurPred = "ph_dur_pred";
        sessionInput->outputs.emplace(outParamPhDurPred);

        std::unique_lock<std::shared_mutex> lock(impl.mutex);
        if (!impl.predictorSession || !impl.predictorSession->isOpen()) {
            setState(Failed);
            Log.srtCritical("%1 start: predictor session is not initialized", kLogPrefix);
            return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceStartFailed,
                              "[Duration] predictor session is not initialized",
                              {}, "duration");
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

        auto durationResult = srt::core::NO<Dur::DurationResult>::create();

        // Get session results
        if (!sessionTaskResult) {
            setState(Failed);
            Log.srtCritical("%1 start: predictor session result is nullptr", kLogPrefix);
            return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceOutputEmpty,
                              "[Duration] predictor session result is nullptr",
                              {}, "duration");
        }
        if (sessionTaskResult->objectName() != Onnx::API_NAME) {
            setState(Failed);
            Log.srtCritical("%1 start: invalid result API name: %2",
                            kLogPrefix, sessionTaskResult->objectName());
            return srt::core::Error::inferenceError(srt::core::ErrorCode::InvalidArgument,
                              "[Duration] invalid result API name",
                              {}, "duration");
        }
        auto sessionResult = sessionTaskResult.as<Onnx::SessionResult>();
        if (!sessionResult) {
            setState(Failed);
            Log.srtCritical("%1 start: type mismatch, expected SessionResult", kLogPrefix);
            return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceRunFailed,
                              "[Duration] type mismatch, expected SessionResult",
                              {}, "duration");
        }
        if (auto it_pred = sessionResult->outputs.find(outParamPhDurPred);
            it_pred != sessionResult->outputs.end()) {
            // Extract onnx model result and copy to duration final result vector (float -> double)
            auto output = std::move(it_pred->second);
            if (output->dataType() != srt::core::ITensor::Float) {
                setState(Failed);
                Log.srtCritical("%1 start: model output is not float", kLogPrefix);
                return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceDataTypeMismatch,
                                  "[Duration] model output is not float",
                                  {}, "duration");
            }
            const auto view = output->view<float>();
            if (view.empty()) {
                setState(Failed);
                Log.srtCritical("%1 start: model output is empty", kLogPrefix);
                return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceOutputEmpty,
                                  "[Duration] model output is empty",
                                  {}, "duration");
            }
            auto &durationVector = durationResult->durations;
            durationVector.assign(view.begin(), view.end());
            // Scale the results to adapt to original word sizes
            size_t begin = 0;
            size_t end = 0;
            for (const auto &word : durationInput->words) {
                if (word.phones.empty()) {
                    setState(Failed);
                    Log.srtCritical("%1 start: error scaling duration results: index out of bounds", kLogPrefix);
                    return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceInputInvalid,
                                      "[Duration] error scaling duration results: index out of bounds",
                                      {}, "duration");
                }
                auto phNum = word.phones.size();
                auto wordDur = ds::infer::inferutil::getWordDuration(word);
                end = begin + phNum;
                if (begin >= durationVector.size() || end > durationVector.size()) {
                    break;
                }
                double predWordDur = 0.0;
                for (size_t i = begin; i < end; ++i) {
                    predWordDur += durationVector[i];
                }
                if (predWordDur == 0 || std::isnan(predWordDur) || std::isinf(predWordDur)) {
                    setState(Failed);
                    Log.srtCritical("%1 start: error scaling duration results: invalid predicted word duration: %2",
                                    kLogPrefix, predWordDur);
                    return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceRunFailed,
                                      "[Duration] error scaling duration results: "
                                      "invalid predicted word duration: " +
                                          std::to_string(predWordDur),
                                      {}, "duration");
                }
                const double scaleFactor = wordDur / predWordDur;
                for (size_t i = begin; i < end; ++i) {
                    durationVector[i] *= scaleFactor;
                }
                begin = end;
            }
        } else {
            setState(Failed);
            Log.srtCritical("%1 start: output 'ph_dur_pred' not found in session result", kLogPrefix);
            return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceOutputEmpty,
                              "[Duration] output 'ph_dur_pred' not found in session result",
                              {}, "duration");
        }

        const auto predictedPhoneCount = durationResult->durations.size();
        if (predictedPhoneCount != phoneCount) {
            setState(Failed);
            Log.srtCritical("%1 start: predicted phoneme count mismatch: expected %2, got %3",
                            kLogPrefix, phoneCount, predictedPhoneCount);
            return srt::core::Error::inferenceError(srt::core::ErrorCode::InferenceRunFailed,
                              stdc::formatN("[Duration] predicted phoneme count mismatch: expected %1, got %2",
                                            phoneCount, predictedPhoneCount),
                              {}, "duration");
        }
        impl.result = durationResult;

        setState(Idle);
        return durationResult;
    }

    srt::core::Expected<void> DurationInference::startAsync(const srt::core::NO<srt::core::TaskStartInput> &input,
                                                      const StartAsyncCallback &callback) {
        // TODO:
        return srt::core::Error(srt::core::ErrorCode::NotImplemented, "not implemented");
    }

    bool DurationInference::stop() {
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

    srt::core::NO<srt::core::TaskResult> DurationInference::result() const {
        auto &impl = *m_impl;
        std::shared_lock<std::shared_mutex> lock(impl.mutex);
        return impl.result;
    }

}
