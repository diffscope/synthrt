#include "DurationTask.h"

#include <cmath>
#include <fstream>
#include <mutex>
#include <numeric>
#include <shared_mutex>
#include <tuple>
#include <utility>

#include <stdcorelib/str.h>
#include <stdcorelib/path.h>

#include <dsinfer/Support/ErrorCode.h>
#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>
#include <dsinfer/Api/Inferences/Duration/1/DurationApiL1.h>
#include <dsinfer/Api/Inferences/Duration/1/DurationOnnxApiL1.h>
#include <dsinfer/Api/Drivers/Onnx/OnnxDriverApi.h>
#include <dsinfer/Inference/InferenceDriver.h>
#include <dsinfer/Inference/InferenceSession.h>
#include <dsinfer/Core/Tensor.h>

#include <inferutil/Driver.h>
#include <inferutil/InputWord.h>
#include <inferutil/LinguisticEncoder.h>
#include <inferutil/SpeakerEmbedding.h>
#include <inferutil/Algorithm.h>

#include "DurationInference.h"

namespace ds {

    namespace Co = Api::Common::L1;
    namespace Dur = Api::Duration::L1;
    namespace Onnx = Api::Onnx;

    static inline srt::Expected<const Dur::Onnx::Configuration *>
        getConfig(const srt::InferenceSpec &spec) {

        const auto genericConfig = spec.configuration();
        if (!genericConfig) {
            return srt::Error(srt::Error::InvalidArgument, "duration configuration is nullptr");
        }
        if (genericConfig->triplet() != Dur::Onnx::Triplet) {
            return srt::Error(srt::Error::InvalidArgument, "invalid duration configuration");
        }
        return static_cast<const Dur::Onnx::Configuration *>(genericConfig);
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

    // Hands the duration predictor one word level input that the word level linguistic encoder
    // already built. Both models describe the same word structure of the score, so the tensor is
    // shared instead of rebuilt and the two cannot disagree about the division or the budget.
    static inline srt::Expected<void> shareWordInput(const Onnx::SessionStartInput &source,
                                                     Onnx::SessionStartInput &target,
                                                     const char *name) {
        const auto it = source.inputs.find(name);
        if (it == source.inputs.end()) {
            return srt::Error(ds::ErrorCode::ProcessingFailed,
                              stdc::formatN(R"(the linguistic encoder input lacks "%1")", name));
        }
        target.inputs.emplace(it->first, it->second);
        return {};
    }

    DurationTask::DurationTask(DurationInference &inference) : DurationTask(inference, {}) {
    }

    DurationTask::DurationTask(DurationInference &inference, const Dur::ImportOptions &options)
        : ITask(Dur::Contract), m_inference(&inference) {
        m_speakerMapping = options.speakerMapping;
    }

    DurationTask::~DurationTask() {
        (void) stop();
        (void) waitForFinished();
    }

    srt::Expected<void> DurationTask::initialize(const Dur::InitArgs &args) {
        return initialize(static_cast<const srt::TaskInitArgs &>(args));
    }

    srt::Expected<std::unique_ptr<Dur::Result>> DurationTask::start(const Dur::StartInput &input) {
        auto result = ITask::start(input);
        if (!result) {
            return result.takeError();
        }
        return std::unique_ptr<Dur::Result>(static_cast<Dur::Result *>(result.take().release()));
    }

    srt::Expected<void> DurationTask::startAsync(std::shared_ptr<const Dur::StartInput> input,
                                                 Dur::Executive::AsyncCallback callback) {
        if (!callback) {
            return srt::Error(srt::Error::InvalidArgument,
                              "duration asynchronous callback must not be empty");
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
                                  callback(std::unique_ptr<Dur::Result>(
                                      static_cast<Dur::Result *>(result.take().release())));
                              });
        if (!started) {
            return started.takeError();
        }
        return {};
    }

    srt::Expected<void> DurationTask::initialize(const srt::TaskInitArgs &args) {
        if (args.contract() != Dur::Contract) {
            return srt::Error(
                srt::Error::InvalidArgument,
                stdc::formatN(R"(invalid duration initialization payload: expected %1, got %2)",
                              Dur::Contract.toString(), args.contract().toString()));
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

        // Open duration session (encoder)
        m_encoderSession = m_driver->createSession();
        Onnx::SessionOpenArgs encoderOpenArgs;
        encoderOpenArgs.useCpu = false;
        if (auto res = m_encoderSession->open(config->encoder, encoderOpenArgs); !res) {
            ITask::setState(ITask::Failed);
            return res;
        }

        // Open duration session (predictor)
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
        DurationTask::execute(const srt::TaskStartInput &input, const srt::TaskStopToken &) {

        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            if (!m_driver) {
                setState(Failed);
                return srt::Error(ds::ErrorCode::NotInitialized,
                                  "inference driver not initialized");
            }
        }

        ITask::setState(ITask::Running);

        auto expConfig = getConfig(m_inference->spec());
        if (!expConfig) {
            setState(Failed);
            return expConfig.takeError();
        }
        const auto config = expConfig.take();

        if (input.contract() != Dur::Contract) {
            ITask::setState(ITask::Failed);
            return srt::Error(
                srt::Error::InvalidArgument,
                stdc::formatN(R"(invalid duration start payload: expected %1, got %2)",
                              Dur::Contract.toString(), input.contract().toString()));
        }

        const auto &durationInput = *input.as<Dur::StartInput>();

        auto sessionInput = std::make_shared<Onnx::SessionStartInput>();

        double frameWidth = config->frameWidth;
        if (!std::isfinite(frameWidth) || frameWidth <= 0) {
            setState(Failed);
            return srt::Error(srt::Error::InvalidArgument, "frame width must be positive");
        }

        // Run the linguistic encoder.
        if (auto exp = inferutil::preprocessLinguisticWord(durationInput.words, config->phonemes,
                                                           config->languages, config->useLanguageId,
                                                           frameWidth);
            exp) {
            // Run Linguistic Encoder Inference
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            if (!m_encoderSession || !m_encoderSession->isOpen()) {
                setState(Failed);
                return srt::Error(ds::ErrorCode::NotInitialized,
                                  "duration linguistic encoder session is not initialized");
            }
            auto linguisticInput = exp.take();
            if (auto encoderSessionExp = inferutil::runEncoder(
                    m_encoderSession.get(), *linguisticInput, /* out */ sessionInput);
                !encoderSessionExp) {
                setState(Failed);
                return encoderSessionExp.takeError().withContext("the linguistic encoder failed");
            }

            // The predictor receives the word tensors that the encoder already built, so a
            // configuration that does not describe the exported predictor is reported by the
            // session instead of running the predictor on a wrong word structure.
            if (config->useWordDiv) {
                if (auto res = shareWordInput(*linguisticInput, *sessionInput, "word_div"); !res) {
                    setState(Failed);
                    return res.takeError();
                }
            }
            if (config->useWordDur) {
                if (auto res = shareWordInput(*linguisticInput, *sessionInput, "word_dur"); !res) {
                    setState(Failed);
                    return res.takeError();
                }
            }
        } else {
            setState(Failed);
            return exp.takeError().withContext("failed to build the linguistic input");
        }

        // Prepare and run the duration predictor.
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
                            const auto name =
                                inferutil::resolveSpeakerName(speaker.name, m_speakerMapping);
                            if (auto speakerIt = config->speakers.find(*name);
                                speakerIt != config->speakers.end()) {
                                const auto &embedding = speakerIt->second;
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
                            } else {
                                setState(Failed);
                                return srt::Error(ds::ErrorCode::InvalidInput,
                                                  "invalid speaker name: " + *name);
                            }
                        }
                        ++currPhoneIndex;
                    }
                }
                sessionInput->inputs["spk_embed"] = tensor;
            } else {
                return exp.takeError().withContext(R"(failed to build the "spk_embed" input)");
            }
        }

        constexpr const char *outParamPhDurPred = "ph_dur_pred";
        sessionInput->outputs.emplace(outParamPhDurPred);

        std::unique_lock<std::shared_mutex> lock(m_mutex);
        if (!m_predictorSession || !m_predictorSession->isOpen()) {
            setState(Failed);
            return srt::Error(ds::ErrorCode::NotInitialized,
                              "duration predictor session is not initialized");
        }

        std::unique_ptr<srt::TaskResult> sessionTaskResult;
        auto sessionExp = m_predictorSession->start(*sessionInput);
        if (!sessionExp) {
            setState(Failed);
            return sessionExp.takeError();
        } else {
            sessionTaskResult = sessionExp.take();
        }

        auto durationResult = std::make_unique<Dur::Result>();

        // Get session results
        if (!sessionTaskResult) {
            setState(Failed);
            return srt::Error(ds::ErrorCode::SessionFailed,
                              "duration predictor session result is nullptr");
        }
        if (sessionTaskResult->contract() != Onnx::Contract) {
            setState(Failed);
            return srt::Error(srt::Error::InvalidArgument, "invalid result API name");
        }
        auto sessionResult = sessionTaskResult->as<Onnx::SessionResult>();
        if (auto predictionIt = sessionResult->outputs.find(outParamPhDurPred);
            predictionIt != sessionResult->outputs.end() && predictionIt->second) {
            // Extract onnx model result and copy to duration final result vector (float -> double)
            auto output = std::move(predictionIt->second);
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
            // Scale the results to adapt to original word sizes. Only the shares of one word are
            // kept, so this step is independent of the unit the model predicts in.
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

    srt::Expected<void> DurationTask::quit() {
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

    srt::Expected<void> DurationTask::wait() {
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

    DurationTask::State DurationTask::classifyOutcome(
        const srt::Expected<std::unique_ptr<srt::TaskResult>> &outcome,
        bool stopRequested) noexcept {
        if (!outcome && stopRequested) {
            return Canceled;
        }
        return outcome ? Succeeded : Failed;
    }

}
