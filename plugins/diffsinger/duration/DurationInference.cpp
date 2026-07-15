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

namespace srt::svs {

    namespace Co = Api::Common::L1;
    namespace Dur = Api::Duration::L1;
    namespace Onnx = srt::driver::onnx;
    namespace DiffSinger = Api::DiffSinger::L1;

    static srt::LogCategory Log("diffsinger.duration");

    static inline srt::core::Expected<srt::core::NO<Dur::DurationConfiguration>>
        getConfig(const srt::svs::InferenceSpec *spec) {

        const auto genericConfig = spec->configuration();
        if (!genericConfig) {
            return srt::core::Error(srt::core::ErrorCode::InvalidArgument,
                              "[Duration] configuration is nullptr");
        }
        if (!(genericConfig->className() == Dur::API_CLASS &&
              genericConfig->objectName() == Dur::API_NAME)) {
            return srt::core::Error(srt::core::ErrorCode::InvalidArgument,
                              "[Duration] invalid configuration class/name");
        }
        return genericConfig.as<Dur::DurationConfiguration>();
    }

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
                return srt::core::Error(srt::core::ErrorCode::InferenceInputInvalid,
                                  "[Duration] failed to fill rest notes");
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
        : Inference(spec), _impl(std::make_unique<Impl>()) {
    }

    DurationInference::~DurationInference() = default;

    srt::core::Expected<void> DurationInference::initialize(const srt::core::NO<srt::core::TaskInitArgs> &args) {
        __stdc_impl_t;
        // Currently, no args to process. But we still need to enforce callers to pass the correct
        // args type.
        if (!args) {
            return srt::core::Error(srt::core::ErrorCode::InvalidArgument,
                              "[Duration] task init args is nullptr");
        }
        if (auto name = args->objectName(); name != Dur::API_NAME) {
            return srt::core::Error(
                srt::core::ErrorCode::InvalidArgument,
                stdc::formatN(R"([Duration] invalid task init args name: expected "%1", got "%2")",
                              Dur::API_NAME, name));
        }
        auto durationArgs = args.as<Dur::DurationInitArgs>();

        std::unique_lock<std::shared_mutex> lock(impl.mutex);

        // If there are existing result, they will be cleared.
        impl.result.reset();

        if (auto res = ds::infer::inferutil::getInferenceDriver(this); res) {
            impl.driver = res.take();
        } else {
            setState(Failed);
            Log.srtCritical("[Duration] initialize: failed to get inference driver: %1",
                            res.error().message());
            return res.takeError();
        }

        // Get duration config
        auto expConfig = getConfig(spec());
        if (!expConfig) {
            setState(Failed);
            Log.srtCritical("[Duration] initialize: %1", expConfig.error().message());
            return expConfig.takeError();
        }
        const auto config = expConfig.take();

        // Open duration session (encoder)
        impl.encoderSession = impl.driver->createSession();
        auto encoderOpenArgs = srt::core::NO<Onnx::SessionOpenArgs>::create();
        encoderOpenArgs->useCpu = false;
        if (auto res = impl.encoderSession->open(config->encoder, encoderOpenArgs); !res) {
            setState(Failed);
            Log.srtCritical("[Duration] initialize: failed to open encoder session for model %1",
                            stdc::path::to_utf8(config->encoder));
            return res;
        }

        // Open duration session (predictor)
        impl.predictorSession = impl.driver->createSession();
        auto predictorOpenArgs = srt::core::NO<Onnx::SessionOpenArgs>::create();
        predictorOpenArgs->useCpu = false;
        if (auto res = impl.predictorSession->open(config->predictor, predictorOpenArgs); !res) {
            setState(Failed);
            Log.srtCritical("[Duration] initialize: failed to open predictor session for model %1",
                            stdc::path::to_utf8(config->predictor));
            return res;
        }

        // Initialize inference state
        setState(Idle);

        // return success
        return srt::core::Expected<void>();
    }

    srt::core::Expected<srt::core::NO<srt::core::TaskResult>>
        DurationInference::start(const srt::core::NO<srt::core::TaskStartInput> &input) {

        __stdc_impl_t;

        {
            std::shared_lock<std::shared_mutex> lock(impl.mutex);
            if (!impl.driver) {
                setState(Failed);
                Log.srtCritical("[Duration] start: inference driver not initialized");
                return srt::core::Error(srt::core::ErrorCode::InferenceStartFailed,
                                  "[Duration] inference driver not initialized");
            }
        }

        setState(Running);

        // Get duration config
        auto expConfig = getConfig(spec());
        if (!expConfig) {
            setState(Failed);
            Log.srtCritical("[Duration] start: %1", expConfig.error().message());
            return expConfig.takeError();
        }
        const auto config = expConfig.take();

        if (!input) {
            setState(Failed);
            Log.srtCritical("[Duration] start: input is nullptr");
            return srt::core::Error(srt::core::ErrorCode::InvalidArgument,
                              "[Duration] input is nullptr");
        }

        if (const auto &name = input->objectName(); name != Dur::API_NAME) {
            setState(Failed);
            Log.srtCritical("[Duration] start: invalid input name: expected %1, got %2",
                            Dur::API_NAME, name);
            return srt::core::Error(
                srt::core::ErrorCode::InvalidArgument,
                stdc::formatN(R"([Duration] invalid input name: expected "%1", got "%2")",
                              Dur::API_NAME, name));
        }

        auto durationInput = input.as<Dur::DurationStartInput>();
        // ...

        auto sessionInput = srt::core::NO<Onnx::SessionStartInput>::create();

        double frameWidth = config->frameWidth;
        if (!std::isfinite(frameWidth) || frameWidth <= 0) {
            setState(Failed);
            Log.srtCritical("[Duration] start: frame width must be positive");
            return srt::core::Error(srt::core::ErrorCode::InvalidArgument,
                              "[Duration] frame width must be positive");
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
                Log.srtCritical("[Duration] start: linguistic encoder session is not initialized");
                return srt::core::Error(srt::core::ErrorCode::InferenceStartFailed,
                                  "[Duration] linguistic encoder session is not initialized");
            }
            if (auto encoderSessionExp =
                    ds::infer::inferutil::runEncoder(impl.encoderSession, exp.take(),
                                                  /* out */ sessionInput);
                !encoderSessionExp) {
                setState(Failed);
                Log.srtCritical("[Duration] start: runEncoder failed: %1",
                                encoderSessionExp.error().message());
                return encoderSessionExp.takeError();
            }
        } else {
            setState(Failed);
            Log.srtCritical("[Duration] start: preprocessLinguisticWord failed: %1",
                            exp.error().message());
            return exp.takeError();
        }

        // Part 2: Duration Inference
        if (auto exp = preprocessPhonemeMidi(durationInput->words); exp) {
            sessionInput->inputs["ph_midi"] = exp.take();
        } else {
            setState(Failed);
            Log.srtCritical("[Duration] start: preprocessPhonemeMidi failed: %1",
                            exp.error().message());
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
                    Log.srtCritical("[Duration] start: failed to create spk_embed tensor");
                    return srt::core::Error(srt::core::ErrorCode::InferenceTensorCreateFailed,
                                      "[Duration] failed to create spk_embed tensor");
                }

                // mix speaker embedding
                int currPhoneIndex = 0;
                for (const auto &word : durationInput->words) {
                    for (const auto &phone : word.phones) {
                        if (phone.speakers.empty()) {
                            setState(Failed);
                            Log.srtCritical("[Duration] start: phoneme %1 missing speakers",
                                            phone.token);
                            return srt::core::Error(
                                srt::core::ErrorCode::InferenceSpeakerNotFound,
                                stdc::formatN("[Duration] phoneme %1 missing speakers", phone.token));
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
                                Log.srtCritical("[Duration] start: speaker %1 not found for "
                                                "phoneme %2 and no inline embedding provided",
                                                speaker.name, phone.token);
                                return srt::core::Error(
                                    srt::core::ErrorCode::InferenceSpeakerNotFound,
                                    stdc::formatN("[Duration] speaker %1 not found in voice bank "
                                                  "and no inline embedding provided (phoneme %2)",
                                                  speaker.name, phone.token));
                            }

                            const auto &embedding = *embeddingPtr;
                            if (embedding.size() != static_cast<size_t>(config->hiddenSize)) {
                                setState(Failed);
                                Log.srtCritical("[Duration] start: speaker embedding vector length does not match hiddenSize");
                                return srt::core::Error(srt::core::ErrorCode::InferenceInputInvalid,
                                                  "[Duration] speaker embedding vector length does not "
                                                  "match hiddenSize");
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
                Log.srtCritical("[Duration] start: failed to create spk_embed tensor shape: %1",
                                exp.error().message());
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
            Log.srtCritical("[Duration] start: predictor session is not initialized");
            return srt::core::Error(srt::core::ErrorCode::InferenceStartFailed,
                              "[Duration] predictor session is not initialized");
        }

        srt::core::NO<srt::core::TaskResult> sessionTaskResult;
        auto sessionExp = impl.predictorSession->start(sessionInput);
        if (!sessionExp) {
            setState(Failed);
            Log.srtCritical("[Duration] start: predictor session->start failed: %1",
                            sessionExp.error().message());
            return sessionExp.takeError();
        } else {
            sessionTaskResult = sessionExp.take();
        }

        auto durationResult = srt::core::NO<Dur::DurationResult>::create();

        // Get session results
        if (!sessionTaskResult) {
            setState(Failed);
            Log.srtCritical("[Duration] start: predictor session result is nullptr");
            return srt::core::Error(srt::core::ErrorCode::InferenceOutputEmpty,
                              "[Duration] predictor session result is nullptr");
        }
        if (sessionTaskResult->objectName() != Onnx::API_NAME) {
            setState(Failed);
            Log.srtCritical("[Duration] start: invalid result API name: %1",
                            sessionTaskResult->objectName());
            return srt::core::Error(srt::core::ErrorCode::InvalidArgument,
                              "[Duration] invalid result API name");
        }
        auto sessionResult = sessionTaskResult.as<Onnx::SessionResult>();
        if (auto it_pred = sessionResult->outputs.find(outParamPhDurPred);
            it_pred != sessionResult->outputs.end()) {
            // Extract onnx model result and copy to duration final result vector (float -> double)
            auto output = std::move(it_pred->second);
            if (output->dataType() != srt::core::ITensor::Float) {
                setState(Failed);
                Log.srtCritical("[Duration] start: model output is not float");
                return srt::core::Error(srt::core::ErrorCode::InferenceDataTypeMismatch,
                                  "[Duration] model output is not float");
            }
            const auto view = output->view<float>();
            if (view.empty()) {
                setState(Failed);
                Log.srtCritical("[Duration] start: model output is empty");
                return srt::core::Error(srt::core::ErrorCode::InferenceOutputEmpty,
                                  "[Duration] model output is empty");
            }
            auto &durationVector = durationResult->durations;
            durationVector.assign(view.begin(), view.end());
            // Scale the results to adapt to original word sizes
            size_t begin = 0;
            size_t end = 0;
            for (const auto &word : durationInput->words) {
                if (word.phones.empty()) {
                    setState(Failed);
                    Log.srtCritical("[Duration] start: error scaling duration results: index out of bounds");
                    return srt::core::Error(srt::core::ErrorCode::InferenceInputInvalid,
                                      "[Duration] error scaling duration results: index out of bounds");
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
                    Log.srtCritical("[Duration] start: error scaling duration results: invalid predicted word duration: %1",
                                    predWordDur);
                    return srt::core::Error(srt::core::ErrorCode::InferenceRunFailed,
                                      "[Duration] error scaling duration results: "
                                      "invalid predicted word duration: " +
                                          std::to_string(predWordDur));
                }
                const double scaleFactor = wordDur / predWordDur;
                for (size_t i = begin; i < end; ++i) {
                    durationVector[i] *= scaleFactor;
                }
                begin = end;
            }
        } else {
            setState(Failed);
            Log.srtCritical("[Duration] start: output 'ph_dur_pred' not found in session result");
            return srt::core::Error(srt::core::ErrorCode::InferenceOutputEmpty,
                              "[Duration] output 'ph_dur_pred' not found in session result");
        }

        const auto predictedPhoneCount = durationResult->durations.size();
        if (predictedPhoneCount != phoneCount) {
            setState(Failed);
            Log.srtCritical("[Duration] start: predicted phoneme count mismatch: expected %1, got %2",
                            phoneCount, predictedPhoneCount);
            return srt::core::Error(srt::core::ErrorCode::InferenceRunFailed,
                              stdc::formatN("[Duration] predicted phoneme count mismatch: expected %1, got %2",
                                            phoneCount, predictedPhoneCount));
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

    srt::core::NO<srt::core::TaskResult> DurationInference::result() const {
        __stdc_impl_t;
        std::shared_lock<std::shared_mutex> lock(impl.mutex);
        return impl.result;
    }

}
