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

#include <dsinfer/Support/ErrorCode.h>
#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>
#include <dsinfer/Api/Inferences/Duration/1/DurationApiL1.h>
#include <dsinfer/Api/Drivers/Onnx/OnnxDriverApi.h>
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

    static inline srt::Expected<const Dur::DurationConfiguration *>
        getConfig(const srt::InferenceSpec &spec) {

        const auto genericConfig = spec.configuration();
        if (!genericConfig) {
            return srt::Error(srt::Error::InvalidArgument, "duration configuration is nullptr");
        }
        if (genericConfig->interface() != Dur::API_INTERFACE ||
            genericConfig->variant() != Dur::API_VARIANT ||
            genericConfig->level() != Dur::API_LEVEL) {
            return srt::Error(srt::Error::InvalidArgument, "invalid duration configuration");
        }
        return static_cast<const Dur::DurationConfiguration *>(genericConfig);
    }

    static inline srt::Expected<std::shared_ptr<ITensor>>
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
                return srt::Error(ds::ErrorCode::ProcessingFailed, "failed to fill rest notes");
            }
        }

        std::vector<int64_t> shape{1, static_cast<int64_t>(phMidi.size())};
        if (auto exp = Tensor::createFromView<int64_t>(shape, stdc::array_view<int64_t>{phMidi});
            exp) {
            return exp.take();
        } else {
            return exp.takeError().withContext("failed to build the phoneme midi tensor");
        }
    }

    class DurationInference::Impl {
    public:
        InferenceDriver *driver = nullptr;
        std::unique_ptr<InferenceSession> encoderSession;
        std::unique_ptr<InferenceSession> predictorSession;
        mutable std::shared_mutex mutex;
    };

    DurationInference::DurationInference(srt::InferenceSpec &spec)
        : Inference(spec), _impl(std::make_unique<Impl>()) {
    }

    DurationInference::~DurationInference() = default;

    srt::Expected<void> DurationInference::initialize(const srt::TaskInitArgs &args) {
        stdc_impl_t;
        // Currently, no args to process. But we still need to enforce callers to pass the correct
        // args type.
        if (args.type() != Dur::API_INTERFACE || args.version() != Dur::API_LEVEL) {
            return srt::Error(
                srt::Error::InvalidArgument,
                stdc::formatN(R"(invalid duration task init args name: expected "%1", got "%2")",
                              Dur::API_INTERFACE, args.type()));
        }

        std::unique_lock<std::shared_mutex> lock(impl.mutex);

        if (auto res = inferutil::getInferenceDriver(this); res) {
            impl.driver = res.take();
        } else {
            ITask::setState(ITask::Failed);
            return res.takeError();
        }

        // Get duration config
        auto expConfig = getConfig(spec());
        if (!expConfig) {
            ITask::setState(ITask::Failed);
            return expConfig.takeError();
        }
        const auto config = expConfig.take();

        // Open duration session (encoder)
        impl.encoderSession = impl.driver->createSession();
        Onnx::SessionOpenArgs encoderOpenArgs;
        encoderOpenArgs.useCpu = false;
        if (auto res = impl.encoderSession->open(config->encoder, encoderOpenArgs); !res) {
            ITask::setState(ITask::Failed);
            return res;
        }

        // Open duration session (predictor)
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
        DurationInference::start(const srt::TaskStartInput &input) {

        stdc_impl_t;

        {
            std::shared_lock<std::shared_mutex> lock(impl.mutex);
            if (!impl.driver) {
                setState(Failed);
                return srt::Error(ds::ErrorCode::NotInitialized,
                                  "inference driver not initialized");
            }
        }

        ITask::setState(ITask::Running);

        // Get duration config
        auto expConfig = getConfig(spec());
        if (!expConfig) {
            setState(Failed);
            return expConfig.takeError();
        }
        const auto config = expConfig.take();

        if (input.type() != Dur::API_INTERFACE || input.version() != Dur::API_LEVEL) {
            ITask::setState(ITask::Failed);
            return srt::Error(
                srt::Error::InvalidArgument,
                stdc::formatN(R"(invalid duration task init args name: expected "%1", got "%2")",
                              Dur::API_INTERFACE, input.type()));
        }

        const auto &durationInput = *input.as<Dur::DurationStartInput>();
        // ...

        auto sessionInput = std::make_shared<Onnx::SessionStartInput>();

        double frameWidth = config->frameWidth;
        if (!std::isfinite(frameWidth) || frameWidth <= 0) {
            setState(Failed);
            return srt::Error(srt::Error::InvalidArgument, "frame width must be positive");
        }

        // Part 1: Linguistic Encoder Inference
        if (auto exp = inferutil::preprocessLinguisticWord(durationInput.words, config->phonemes,
                                                           config->languages, config->useLanguageId,
                                                           frameWidth);
            exp) {
            // Run Linguistic Encoder Inference
            std::unique_lock<std::shared_mutex> lock(impl.mutex);
            if (!impl.encoderSession || !impl.encoderSession->isOpen()) {
                setState(Failed);
                return srt::Error(ds::ErrorCode::NotInitialized,
                                  "duration linguistic encoder session is not initialized");
            }
            auto linguisticInput = exp.take();
            if (auto encoderSessionExp = inferutil::runEncoder(
                    impl.encoderSession.get(), *linguisticInput, /* out */ sessionInput);
                !encoderSessionExp) {
                setState(Failed);
                return encoderSessionExp.takeError().withContext("the linguistic encoder failed");
            }
        } else {
            setState(Failed);
            return exp.takeError().withContext("failed to build the linguistic input");
        }

        // Part 2: Duration Inference
        if (auto exp = preprocessPhonemeMidi(durationInput.words); exp) {
            sessionInput->inputs["ph_midi"] = exp.take();
        } else {
            setState(Failed);
            return exp.takeError().withContext(R"(failed to build the "ph_midi" input)");
        }

        auto phoneCount = inferutil::getPhoneCount(durationInput.words);
        if (config->useSpeakerEmbedding) {
            std::vector<int64_t> shape = {1, static_cast<int64_t>(phoneCount), config->hiddenSize};
            if (auto exp = Tensor::create(ITensor::Float, shape); exp) {
                // get tensor buffer
                auto tensor = exp.take();
                auto buffer = tensor->data<float>();
                if (!buffer) {
                    setState(Failed);
                    return srt::Error(ds::ErrorCode::ProcessingFailed,
                                      "failed to create spk_embed tensor");
                }

                // mix speaker embedding
                int currPhoneIndex = 0;
                for (const auto &word : durationInput.words) {
                    for (const auto &phone : word.phones) {
                        if (phone.speakers.empty()) {
                            setState(Failed);
                            return srt::Error(
                                ds::ErrorCode::InvalidInput,
                                stdc::formatN("phoneme %1 missing speakers", phone.token));
                        }
                        for (const auto &speaker : phone.speakers) {
                            if (auto it_speaker = config->speakers.find(speaker.name);
                                it_speaker != config->speakers.end()) {
                                const auto &embedding = it_speaker->second;
                                if (embedding.size() != config->hiddenSize) {
                                    setState(Failed);
                                    return srt::Error(ds::ErrorCode::ShapeMismatch,
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
                return exp.takeError().withContext(R"(failed to build the "spk_embed" input)");
            }
        } else {
            // Nothing to do: speaker embedding is not supported
        }

        constexpr const char *outParamPhDurPred = "ph_dur_pred";
        sessionInput->outputs.emplace(outParamPhDurPred);

        std::unique_lock<std::shared_mutex> lock(impl.mutex);
        if (!impl.predictorSession || !impl.predictorSession->isOpen()) {
            setState(Failed);
            return srt::Error(ds::ErrorCode::NotInitialized,
                              "duration predictor session is not initialized");
        }

        std::unique_ptr<srt::TaskResult> sessionTaskResult;
        auto sessionExp = impl.predictorSession->start(*sessionInput);
        if (!sessionExp) {
            setState(Failed);
            return sessionExp.takeError();
        } else {
            sessionTaskResult = sessionExp.take();
        }

        auto durationResult = std::make_unique<Dur::DurationResult>();

        // Get session results
        if (!sessionTaskResult) {
            setState(Failed);
            return srt::Error(ds::ErrorCode::SessionFailed,
                              "duration predictor session result is nullptr");
        }
        if (sessionTaskResult->type() != Onnx::API_NAME ||
            sessionTaskResult->version() != Onnx::API_VERSION) {
            setState(Failed);
            return srt::Error(srt::Error::InvalidArgument, "invalid result API name");
        }
        auto *sessionResult = sessionTaskResult->as<Onnx::SessionResult>();
        if (auto it_pred = sessionResult->outputs.find(outParamPhDurPred);
            it_pred != sessionResult->outputs.end()) {
            // Extract onnx model result and copy to duration final result vector (float -> double)
            auto output = std::move(it_pred->second);
            if (output->dataType() != ITensor::Float) {
                setState(Failed);
                return srt::Error(ds::ErrorCode::SessionFailed, "model output is not float");
            }
            const auto view = output->view<float>();
            if (view.empty()) {
                setState(Failed);
                return srt::Error(ds::ErrorCode::SessionFailed, "model output is empty");
            }
            auto &durationVector = durationResult->durations;
            durationVector.assign(view.begin(), view.end());
            // Scale the results to adapt to original word sizes
            size_t begin = 0;
            size_t end = 0;
            for (const auto &word : durationInput.words) {
                if (word.phones.empty()) {
                    setState(Failed);
                    return srt::Error(ds::ErrorCode::ProcessingFailed,
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
                    return srt::Error(ds::ErrorCode::ProcessingFailed,
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
            return srt::Error(ds::ErrorCode::SessionFailed, "invalid result output");
        }

        const auto predictedPhoneCount = durationResult->durations.size();
        if (predictedPhoneCount != phoneCount) {
            setState(Failed);
            return srt::Error(ds::ErrorCode::ShapeMismatch,
                              stdc::formatN("predicted phoneme count mismatch: expected %1, got %2",
                                            phoneCount, predictedPhoneCount));
        }
        ITask::setState(ITask::Succeeded);
        return std::unique_ptr<srt::TaskResult>(std::move(durationResult));
    }

    srt::Expected<void> DurationInference::startAsync(std::shared_ptr<const srt::TaskStartInput>,
                                                      AsyncCallback) {
        return srt::Error(srt::Error::NotImplemented);
    }

    srt::Expected<void> DurationInference::stop() {
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

    srt::Expected<void> DurationInference::waitForFinished() {
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
