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

#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>
#include <dsinfer/Api/Inferences/Duration/1/DurationApiL1.h>
#include <dsinfer/Api/Drivers/Onnx/OnnxDriverApi.h>
#include <dsinfer/Api/Singers/DiffSinger/1/DiffSingerApiL1.h>
#include <dsinfer/Inference/InferenceDriver.h>
#include <dsinfer/Inference/InferenceSession.h>
#include <dsinfer/Core/Tensor.h>

#include <inferutil/Driver.h>
#include <inferutil/InputWord.h>
#include <inferutil/LinguisticEncoder.h>
#include <inferutil/Algorithm.h>

namespace ds {

    namespace Co = Api::Common::L1;
    namespace Dur = Api::Duration::L1;
    namespace Onnx = Api::Onnx;
    namespace DiffSinger = Api::DiffSinger::L1;

    static inline srt::Expected<srt::NO<Dur::DurationConfiguration>>
        getConfig(const srt::InferenceSpec *spec) {

        const auto genericConfig = spec->configuration();
        if (!genericConfig) {
            return srt::Error(srt::Error::InvalidArgument, "duration configuration is nullptr");
        }
        if (!(genericConfig->className() == Dur::API_CLASS &&
              genericConfig->objectName() == Dur::API_NAME)) {
            return srt::Error(srt::Error::InvalidArgument, "invalid duration configuration");
        }
        return genericConfig.as<Dur::DurationConfiguration>();
    }

    static inline srt::Expected<srt::NO<ITensor>>
        preprocessPhonemeMidi(const std::vector<Api::Common::L1::InputWordInfo> &words) {

        auto phoneCount = inferutil::getPhoneCount(words);

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

            if (!inferutil::fillRestMidiWithNearestInPlace<int64_t>(phMidi, isRest)) {
                return srt::Error(srt::Error::SessionError, "failed to fill rest notes");
            }
        }

        std::vector<int64_t> shape{1, static_cast<int64_t>(phMidi.size())};
        if (auto exp = Tensor::createFromView<int64_t>(shape, stdc::array_view<int64_t>{phMidi});
            exp) {
            return exp.take();
        } else {
            return exp.takeError();
        }
    }

    class DurationInference::Impl {
    public:
        srt::NO<Dur::DurationResult> result;
        srt::NO<InferenceDriver> driver;
        srt::NO<InferenceSession> encoderSession;
        srt::NO<InferenceSession> predictorSession;
        mutable std::shared_mutex mutex;
    };

    DurationInference::DurationInference(const srt::InferenceSpec *spec)
        : Inference(spec), _impl(std::make_unique<Impl>()) {
    }

    DurationInference::~DurationInference() = default;

    srt::Expected<void> DurationInference::initialize(const srt::NO<srt::TaskInitArgs> &args) {
        __stdc_impl_t;
        // Currently, no args to process. But we still need to enforce callers to pass the correct
        // args type.
        if (!args) {
            return srt::Error(srt::Error::InvalidArgument, "duration task init args is nullptr");
        }
        if (auto name = args->objectName(); name != Dur::API_NAME) {
            return srt::Error(
                srt::Error::InvalidArgument,
                stdc::formatN(R"(invalid duration task init args name: expected "%1", got "%2")",
                              Dur::API_NAME, name));
        }
        auto durationArgs = args.as<Dur::DurationInitArgs>();

        std::unique_lock<std::shared_mutex> lock(impl.mutex);

        // If there are existing result, they will be cleared.
        impl.result.reset();

        if (auto res = inferutil::getInferenceDriver(this); res) {
            impl.driver = res.take();
        } else {
            setState(Failed);
            return res.takeError();
        }

        // Get duration config
        auto expConfig = getConfig(spec());
        if (!expConfig) {
            setState(Failed);
            return expConfig.takeError();
        }
        const auto config = expConfig.take();

        // Open duration session (encoder)
        impl.encoderSession = impl.driver->createSession();
        auto encoderOpenArgs = srt::NO<Onnx::SessionOpenArgs>::create();
        encoderOpenArgs->useCpu = false;
        if (auto res = impl.encoderSession->open(config->encoder, encoderOpenArgs); !res) {
            setState(Failed);
            return res;
        }

        // Open duration session (predictor)
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

    srt::Expected<srt::NO<srt::TaskResult>>
        DurationInference::start(const srt::NO<srt::TaskStartInput> &input) {

        __stdc_impl_t;

        {
            std::shared_lock<std::shared_mutex> lock(impl.mutex);
            if (!impl.driver) {
                setState(Failed);
                return srt::Error(srt::Error::SessionError, "inference driver not initialized");
            }
        }

        setState(Running);

        // Get duration config
        auto expConfig = getConfig(spec());
        if (!expConfig) {
            setState(Failed);
            return expConfig.takeError();
        }
        const auto config = expConfig.take();

        if (!input) {
            setState(Failed);
            return srt::Error(srt::Error::InvalidArgument, "duration input is nullptr");
        }

        if (const auto &name = input->objectName(); name != Dur::API_NAME) {
            setState(Failed);
            return srt::Error(
                srt::Error::InvalidArgument,
                stdc::formatN(R"(invalid duration task init args name: expected "%1", got "%2")",
                              Dur::API_NAME, name));
        }

        auto durationInput = input.as<Dur::DurationStartInput>();
        // ...

        auto sessionInput = srt::NO<Onnx::SessionStartInput>::create();

        double frameWidth = config->frameWidth;
        if (!std::isfinite(frameWidth) || frameWidth <= 0) {
            setState(Failed);
            return srt::Error(srt::Error::InvalidArgument, "frame width must be positive");
        }

        // Part 1: Linguistic Encoder Inference
        if (auto exp = inferutil::preprocessLinguisticWord(
                durationInput->words, config->phonemes, config->languages, config->useLanguageId,
                frameWidth);
            exp) {
            // Run Linguistic Encoder Inference
            std::unique_lock<std::shared_mutex> lock(impl.mutex);
            if (!impl.encoderSession || !impl.encoderSession->isOpen()) {
                setState(Failed);
                return srt::Error(srt::Error::SessionError,
                                  "duration linguistic encoder session is not initialized");
            }
            if (auto encoderSessionExp =
                    inferutil::runEncoder(impl.encoderSession, exp.take(),
                                                  /* out */ sessionInput);
                !encoderSessionExp) {
                setState(Failed);
                return encoderSessionExp.takeError();
            }
        } else {
            setState(Failed);
            return exp.takeError();
        }

        // Part 2: Duration Inference
        if (auto exp = preprocessPhonemeMidi(durationInput->words); exp) {
            sessionInput->inputs["ph_midi"] = exp.take();
        } else {
            setState(Failed);
            return exp.takeError();
        }

        auto phoneCount = inferutil::getPhoneCount(durationInput->words);
        if (config->useSpeakerEmbedding) {
            std::vector<int64_t> shape = {1, static_cast<int64_t>(phoneCount), config->hiddenSize};
            if (auto exp = Tensor::create(ITensor::Float, shape); exp) {
                // get tensor buffer
                auto tensor = exp.take();
                auto buffer = tensor->mutableData<float>();
                if (!buffer) {
                    setState(Failed);
                    return srt::Error(srt::Error::SessionError,
                                      "failed to create spk_embed tensor");
                }

                // mix speaker embedding
                int currPhoneIndex = 0;
                for (const auto &word : durationInput->words) {
                    for (const auto &phone : word.phones) {
                        if (phone.speakers.empty()) {
                            setState(Failed);
                            return srt::Error(
                                srt::Error::SessionError,
                                stdc::formatN("phoneme %1 missing speakers", phone.token));
                        }
                        for (const auto &speaker : phone.speakers) {
                            if (auto it_speaker = config->speakers.find(speaker.name);
                                it_speaker != config->speakers.end()) {
                                const auto &embedding = it_speaker->second;
                                if (embedding.size() != config->hiddenSize) {
                                    setState(Failed);
                                    return srt::Error(srt::Error::SessionError,
                                                      "speaker embedding vector length does not "
                                                      "match hiddenSize");
                                }
                                for (size_t j = 0; j < embedding.size(); ++j) {
                                    float &val = buffer[currPhoneIndex * embedding.size() + j];
                                    val = std::fmaf(static_cast<float>(speaker.proportion),
                                                    embedding[j], val);
                                }
                            }
                        }
                        ++currPhoneIndex;
                    }
                }
                sessionInput->inputs["spk_embed"] = tensor;
            } else {
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
            return srt::Error(srt::Error::SessionError,
                              "duration predictor session is not initialized");
        }

        srt::NO<srt::TaskResult> sessionTaskResult;
        auto sessionExp = impl.predictorSession->start(sessionInput);
        if (!sessionExp) {
            setState(Failed);
            return sessionExp.takeError();
        } else {
            sessionTaskResult = sessionExp.take();
        }

        auto durationResult = srt::NO<Dur::DurationResult>::create();

        // Get session results
        if (!sessionTaskResult) {
            setState(Failed);
            return srt::Error(srt::Error::SessionError,
                              "duration predictor session result is nullptr");
        }
        if (sessionTaskResult->objectName() != Onnx::API_NAME) {
            setState(Failed);
            return srt::Error(srt::Error::InvalidArgument, "invalid result API name");
        }
        auto sessionResult = sessionTaskResult.as<Onnx::SessionResult>();
        if (auto it_pred = sessionResult->outputs.find(outParamPhDurPred);
            it_pred != sessionResult->outputs.end()) {
            // Extract onnx model result and copy to duration final result vector (float -> double)
            auto output = std::move(it_pred->second);
            if (output->dataType() != ITensor::Float) {
                setState(Failed);
                return srt::Error(srt::Error::SessionError, "model output is not float");
            }
            const auto view = output->view<float>();
            if (view.empty()) {
                setState(Failed);
                return srt::Error(srt::Error::SessionError, "model output is empty");
            }
            auto &durationVector = durationResult->durations;
            durationVector.assign(view.begin(), view.end());
            // Scale the results to adapt to original word sizes
            size_t begin = 0;
            size_t end = 0;
            for (const auto &word : durationInput->words) {
                if (word.phones.empty()) {
                    setState(Failed);
                    return srt::Error(srt::Error::SessionError,
                                      "error scaling duration results: index out of bounds");
                }
                auto phNum = word.phones.size();
                auto wordDur = inferutil::getWordDuration(word);
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
                    return srt::Error(srt::Error::SessionError,
                                      "error scaling duration results: "
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
            return srt::Error(srt::Error::SessionError, "invalid result output");
        }

        const auto predictedPhoneCount = durationResult->durations.size();
        if (predictedPhoneCount != phoneCount) {
            setState(Failed);
            return srt::Error(srt::Error::SessionError,
                              stdc::formatN("predicted phoneme count mismatch: expected %1, got %2",
                                            phoneCount, predictedPhoneCount));
        }
        impl.result = durationResult;

        setState(Idle);
        return durationResult;
    }

    srt::Expected<void> DurationInference::startAsync(const srt::NO<srt::TaskStartInput> &input,
                                                      const StartAsyncCallback &callback) {
        // TODO:
        return srt::Error(srt::Error::NotImplemented);
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

    srt::NO<srt::TaskResult> DurationInference::result() const {
        __stdc_impl_t;
        std::shared_lock<std::shared_mutex> lock(impl.mutex);
        return impl.result;
    }

}