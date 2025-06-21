#include "AcousticInference.h"

#include <fstream>
#include <mutex>
#include <shared_mutex>
#include <utility>

#include <stdcorelib/pimpl.h>
#include <stdcorelib/str.h>
#include <stdcorelib/path.h>

#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>
#include <dsinfer/Api/Inferences/Acoustic/1/AcousticApiL1.h>
#include <dsinfer/Api/Drivers/Onnx/OnnxDriverApi.h>
#include <dsinfer/Api/Singers/DiffSinger/1/DiffSingerApiL1.h>
#include <dsinfer/Inference/InferenceDriver.h>
#include <dsinfer/Inference/InferenceSession.h>
#include <dsinfer/Core/ParamTag.h>
#include <dsinfer/Core/Tensor.h>

#include <InterpreterCommon/Driver.h>
#include <InterpreterCommon/MathUtil.h>
#include <InterpreterCommon/TensorHelper.h>
#include <InterpreterCommon/SpeakerEmbedding.h>

namespace ds {

    namespace Co = Api::Common::L1;
    namespace Ac = Api::Acoustic::L1;
    namespace Onnx = Api::Onnx;
    namespace DiffSinger = Api::DiffSinger::L1;

    static inline size_t getPhoneCount(const std::vector<Co::InputWordInfo> &words) {
        size_t phoneCount = 0;
        for (const auto &word : words) {
            phoneCount += word.phones.size();
        }
        return phoneCount;
    }

    static inline double getWordDuration(const Co::InputWordInfo &word) {
        double wordDuration = 0;
        for (const auto &note : word.notes) {
            wordDuration += note.duration;
        }
        return wordDuration;
    }

    srt::Expected<srt::NO<ITensor>> inline parsePhonemeTokens(
        const std::vector<Co::InputWordInfo> &words,
        const std::map<std::string, int> &name2token) {

        constexpr const char *SP_TOKEN = "SP";
        constexpr const char *AP_TOKEN = "AP";

        using TensorType = int64_t;
        auto phoneCount = getPhoneCount(words);
        auto exp = InterpreterCommon::TensorHelper<TensorType>::createFor1DArray(phoneCount);
        if (!exp) {
            return exp.takeError();
        }
        auto &helper = exp.value();

        for (const auto &word : words) {
            for (const auto &phone : word.phones) {
                // tokens
                std::string tokenWithLang =
                    (phone.language.empty() || phone.token == SP_TOKEN || phone.token == AP_TOKEN)
                        ? phone.token
                        : (phone.language + '/' + phone.token);

                if (const auto it1 = name2token.find(tokenWithLang); it1 != name2token.end()) {
                    // first try finding the phoneme with the language tag (lang/phoneme)
                    helper.write(it1->second);
                } else if (const auto it2 = name2token.find(phone.token); it2 != name2token.end()) {
                    // then try finding the phoneme without the language tag (phoneme)
                    helper.write(it2->second);
                } else {
                    return srt::Error(srt::Error::InvalidArgument, "unknown token " + phone.token);
                }
            }
        }

        if (STDCORELIB_UNLIKELY(!helper.isComplete())) {
            return srt::Error(
                srt::Error::SessionError,
                "parsePhonemeTokens: tensor element count does not match phoneme count");
        }
        return helper.take();
    }

    srt::Expected<srt::NO<ITensor>> inline parsePhonemeLanguages(
        const std::vector<Co::InputWordInfo> &words, const std::map<std::string, int> &languages) {

        auto phoneCount = getPhoneCount(words);
        using TensorType = int64_t;
        auto exp = InterpreterCommon::TensorHelper<TensorType>::createFor1DArray(phoneCount);
        if (!exp) {
            return exp.takeError();
        }
        auto &helper = exp.value();

        for (const auto &word : words) {
            for (const auto &phone : word.phones) {
                if (const auto it = languages.find(phone.language); it != languages.end()) {
                    helper.write(it->second);
                } else {
                    return srt::Error(srt::Error::InvalidArgument,
                                      "unknown language " + phone.token);
                }
            }
        }

        if (STDCORELIB_UNLIKELY(!helper.isComplete())) {
            return srt::Error(
                srt::Error::SessionError,
                "parsePhonemeLanguages: tensor element count does not match phoneme count");
        }

        return helper.take();
    }

    srt::Expected<srt::NO<ITensor>> inline parsePhonemeDurations(
        const std::vector<Co::InputWordInfo> &words, double frameLength,
        int64_t *outTargetLength = nullptr) {

        auto phoneCount = getPhoneCount(words);
        using TensorType = int64_t;
        auto exp = InterpreterCommon::TensorHelper<TensorType>::createFor1DArray(phoneCount);
        if (!exp) {
            return exp.takeError();
        }
        auto &helper = exp.value();

        double phoneDurSum = 0.0;
        int64_t targetLength = 0;

        for (size_t currWordIndex = 0; currWordIndex < words.size(); ++currWordIndex) {
            const auto &word = words[currWordIndex];
            auto wordDuration = getWordDuration(word);

            for (size_t i = 0; i < word.phones.size(); ++i) {

                // durations
                {
                    bool currPhoneIsTheLastPhone = (i == word.phones.size() - 1);
                    auto currPhoneStart = phoneDurSum + word.phones[i].start;
                    auto nextPhoneStart =
                        phoneDurSum +
                        (currPhoneIsTheLastPhone ? wordDuration : word.phones[i + 1].start);
                    if (currPhoneIsTheLastPhone && (currWordIndex + 1 < words.size())) {
                        // If current word is not the last word
                        const auto &nextWord = words[currWordIndex + 1];
                        if (!nextWord.phones.empty()) {
                            nextPhoneStart += nextWord.phones[0].start;
                        }
                    }
                    int64_t currPhoneStartFrames = std::llround(currPhoneStart / frameLength);
                    int64_t nextPhoneStartFrames = std::llround(nextPhoneStart / frameLength);
                    int64_t currPhoneFrames = nextPhoneStartFrames - currPhoneStartFrames;
                    helper.write(currPhoneFrames);
                    targetLength += currPhoneFrames;
                }
            }
            phoneDurSum += wordDuration;
        }

        if (STDCORELIB_UNLIKELY(!helper.isComplete())) {
            return srt::Error(
                srt::Error::SessionError,
                "parsePhonemeDurations: tensor element count does not match phoneme count");
        }

        if (outTargetLength) {
            *outTargetLength = targetLength;
        }
        return helper.take();
    }

    class AcousticInference::Impl {
    public:
        srt::NO<Ac::AcousticResult> result;
        srt::NO<InferenceDriver> driver;
        srt::NO<InferenceSession> session;
        mutable std::shared_mutex mutex;
    };

    AcousticInference::AcousticInference(const srt::InferenceSpec *spec)
        : Inference(spec), _impl(std::make_unique<Impl>()) {
    }

    AcousticInference::~AcousticInference() = default;

    srt::Expected<void> AcousticInference::initialize(const srt::NO<srt::TaskInitArgs> &args) {
        __stdc_impl_t;
        // Currently, no args to process. But we still need to enforce callers to pass the correct
        // args type.
        if (!args) {
            return srt::Error(srt::Error::InvalidArgument, "acoustic task init args is nullptr");
        }
        if (auto name = args->objectName(); name != Ac::API_NAME) {
            return srt::Error(
                srt::Error::InvalidArgument,
                stdc::formatN(R"(invalid acoustic task init args name: expected "%1", got "%2")",
                              Ac::API_NAME, name));
        }
        auto acousticArgs = args.as<srt::TaskInitArgs>();

        // If there are existing result, they will be cleared.
        {
            std::unique_lock<std::shared_mutex> lock(impl.mutex);
            impl.result.reset();
        }

        if (auto res = InterpreterCommon::getInferenceDriver(this); res) {
            impl.driver = res.take();
        } else {
            setState(Failed);
            return res.takeError();
        }

        // Initialize inference state
        setState(Idle);

        // return success
        return srt::Expected<void>();
    }

    srt::Expected<void> AcousticInference::start(const srt::NO<srt::TaskStartInput> &input) {
        __stdc_impl_t;

        if (auto res = InterpreterCommon::getInferenceDriver(this); res) {
            impl.driver = res.take();
        } else {
            setState(Failed);
            return res.takeError();
        }

        setState(Running);

        auto genericConfig = spec()->configuration();
        if (!genericConfig) {
            setState(Failed);
            return srt::Error(srt::Error::InvalidArgument, "acoustic configuration is nullptr");
        }
        if (!(genericConfig->className() == Ac::API_CLASS &&
              genericConfig->objectName() == Ac::API_NAME)) {
            setState(Failed);
            return srt::Error(srt::Error::InvalidArgument, "invalid acoustic configuration");
        }
        auto config = genericConfig.as<Ac::AcousticConfiguration>();

        if (!input) {
            setState(Failed);
            return srt::Error(srt::Error::InvalidArgument, "acoustic input is nullptr");
        }

        if (const auto &name = input->objectName(); name != Ac::API_NAME) {
            setState(Failed);
            return srt::Error(
                srt::Error::InvalidArgument,
                stdc::formatN(R"(invalid acoustic task init args name: expected "%1", got "%2")",
                              Ac::API_NAME, name));
        }

        auto acousticInput = input.as<Ac::AcousticStartInput>();
        // ...

        auto sessionInput = srt::NO<Onnx::SessionStartInput>::create();

        double frameLength = 1.0 * config->hopSize / config->sampleRate;

        // input param: tokens
        if (auto res = parsePhonemeTokens(acousticInput->words, config->phonemes); res) {
            sessionInput->inputs["tokens"] = res.take();
        } else {
            setState(Failed);
            return res.takeError();
        }

        // input param: languages
        if (config->useLanguageId) {
            if (auto res = parsePhonemeLanguages(acousticInput->words, config->languages); res) {
                sessionInput->inputs["languages"] = res.take();
            } else {
                setState(Failed);
                return res.takeError();
            }
        }

        // input param: durations
        int64_t targetLength;

        if (auto res = parsePhonemeDurations(acousticInput->words, frameLength, &targetLength); res) {
            sessionInput->inputs["durations"] = res.take();
        } else {
            setState(Failed);
            return res.takeError();
        }

        // input param: steps
        {
            auto exp = Tensor::createScalar(acousticInput->steps);
            if (!exp) {
                setState(Failed);
                return exp.takeError();
            }
            sessionInput->inputs["steps"] = exp.take();
        }

        // input param: depth
        {
            auto exp = Tensor::createScalar(acousticInput->depth);
            if (!exp) {
                setState(Failed);
                return exp.takeError();
            }
            sessionInput->inputs["depth"] = exp.take();
        }

        // We define some requirements according to config.
        //
        // If the config supports a parameter, the flag is set to false, and
        // when the input contains such valid parameter, the flag is then set to true.
        //
        // If the config does NOT support a parameter, the flag is automatically set to true.
        // No need to check such input.
        const auto hasParam = [&](const ParamTag &tag) -> bool {
            return config->parameters.find(tag) != config->parameters.end();
        };

        bool satisfyGender = !hasParam(Co::Tags::Gender);
        bool satisfyVelocity = !hasParam(Co::Tags::Velocity);
        bool satisfyToneShift = !hasParam(Co::Tags::ToneShift);

        bool satisfyEnergy = !hasParam(Co::Tags::Energy);
        bool satisfyBreathiness = !hasParam(Co::Tags::Breathiness);
        bool satisfyVoicing = !hasParam(Co::Tags::Voicing);
        bool satisfyTension = !hasParam(Co::Tags::Tension);
        bool satisfyMouthOpening = !hasParam(Co::Tags::MouthOpening);

        srt::NO<ITensor> f0TensorForVocoder;

        // Because of tone shifting, the pitch parameter should be last processed,
        // so we use a pointer to store it first.
        const Co::InputParameterInfo *pPitchParam = nullptr;
        const Co::InputParameterInfo *pToneShiftParam = nullptr;

        for (const auto &param : acousticInput->parameters) {
            if (param.tag == Co::Tags::Pitch) {
                // Pitch will be processed later,
                // since it needs to be adjusted according to tone shift.
                pPitchParam = &param;
                continue;
            }
            if (param.tag == Co::Tags::ToneShift) {
                // Tone shift will be processed later.
                if (!satisfyToneShift) {
                    pToneShiftParam = &param;
                }
                continue;
            }

            // Resample the parameters to target time step,
            // and resize to target frame length (fill with last value)
            auto resampled = InterpreterCommon::resample(param.values, param.interval, frameLength,
                                                         targetLength, true);
            if (resampled.empty()) {
                // These parameters are optional
                if (param.tag == Co::Tags::Gender) {
                    // Fill gender with 0
                    auto exp =
                        Tensor::createFilled<float>(std::vector<int64_t>{1, targetLength}, 0.0f);
                    if (!exp) {
                        setState(Failed);
                        return exp.takeError();
                    }
                    sessionInput->inputs["gender"] = exp.take();
                    satisfyGender = true;
                    continue;
                }
                if (param.tag == Co::Tags::Velocity) {
                    // Fill velocity with 0
                    auto exp =
                        Tensor::createFilled<float>(std::vector<int64_t>{1, targetLength}, 1.0f);
                    if (!exp) {
                        setState(Failed);
                        return exp.takeError();
                    }
                    sessionInput->inputs["velocity"] = exp.take();
                    satisfyVelocity = true;
                    continue;
                }
            }
            if (resampled.size() != targetLength) {
                setState(Failed);
                return srt::Error(srt::Error::SessionError, "parameter " +
                                                                std::string(param.tag.name()) +
                                                                " resample failed");
            }

            auto exp = InterpreterCommon::TensorHelper<float>::createFor1DArray(targetLength);
            if (!exp) {
                setState(Failed);
                return exp.takeError();
            }
            auto &helper = exp.value();

            // for other parameters, simply fill them in.
            for (const auto item : std::as_const(resampled)) {
                helper.writeUnchecked(static_cast<float>(item));
            }
            if (!satisfyGender && param.tag == Co::Tags::Gender) {
                sessionInput->inputs["gender"] = helper.take();
                satisfyGender = true;
                continue;
            }
            if (!satisfyVelocity && param.tag == Co::Tags::Velocity) {
                sessionInput->inputs["velocity"] = helper.take();
                satisfyVelocity = true;
                continue;
            }
            if (!satisfyEnergy && param.tag == Co::Tags::Energy) {
                sessionInput->inputs["energy"] = helper.take();
                satisfyEnergy = true;
                continue;
            }
            if (!satisfyBreathiness && param.tag == Co::Tags::Breathiness) {
                sessionInput->inputs["breathiness"] = helper.take();
                satisfyBreathiness = true;
                continue;
            }
            if (!satisfyVoicing && param.tag == Co::Tags::Voicing) {
                sessionInput->inputs["voicing"] = helper.take();
                satisfyVoicing = true;
                continue;
            }
            if (!satisfyTension && param.tag == Co::Tags::Tension) {
                sessionInput->inputs["tension"] = helper.take();
                satisfyTension = true;
                continue;
            }
            if (!satisfyMouthOpening && param.tag == Co::Tags::MouthOpening) {
                sessionInput->inputs["mouth_opening"] = helper.take();
                satisfyMouthOpening = true;
                continue;
            }
        }

        if (pPitchParam) {
            // Has pitch parameter
            const auto &pitchParam = *pPitchParam;
            // Resample pitch
            auto pitchSamples = InterpreterCommon::resample(pitchParam.values, pitchParam.interval,
                                                            frameLength, targetLength, true);
            if (pitchSamples.size() != targetLength) {
                setState(Failed);
                return srt::Error(srt::Error::SessionError, "parameter " +
                                                                std::string(pitchParam.tag.name()) +
                                                                " resample failed");
            }
            // tone shift samples are in cents (1 semitone == 100 cents)
            std::vector<double> toneShiftSamples;
            if (pToneShiftParam) {
                // Needs tone shift
                const auto &toneShiftParam = *pToneShiftParam;
                toneShiftSamples =
                    InterpreterCommon::resample(toneShiftParam.values, toneShiftParam.interval,
                                                frameLength, targetLength, true);
                if (!toneShiftSamples.empty()) {
                    if (toneShiftSamples.size() != targetLength) {
                        setState(Failed);
                        return srt::Error(srt::Error::SessionError,
                                          "parameter " + std::string(toneShiftParam.tag.name()) +
                                              " resample failed");
                    }
                } else {
                    // Tone shift resampled is empty, ignore and do not modify pitch.
                    // Nothing to do here.
                }
            }

            constexpr double a4_freq_hz = 440.0;
            constexpr double midi_a4_note = 69.0;

            // Create f0 tensor for acoustic model
            auto expForAcoustic = InterpreterCommon::TensorHelper<float>::createFor1DArray(targetLength);
            if (!expForAcoustic) {
                setState(Failed);
                return expForAcoustic.takeError();
            }
            auto &acousticHelper = expForAcoustic.value();
            // Convert midi note to hz
            for (const auto midi_note : std::as_const(pitchSamples)) {
                auto f0Acoustic = a4_freq_hz * std::exp2((midi_note - midi_a4_note) / 12.0);
                // Buffer guaranteed not to overflow,
                // given (resampled.size() == targetLength), which has been checked before
                acousticHelper.writeUnchecked(static_cast<float>(f0Acoustic));
            }
            f0TensorForVocoder = acousticHelper.take();
            sessionInput->inputs["f0"] = f0TensorForVocoder; // ref count +1

            // Create f0 tensor for vocoder model
            if (toneShiftSamples.empty()) {
                // No tone shift happened.
                // F0 tensor for vocoder is exactly the same as that for acoustic.
                // Nothing to do here.
            } else {
                // Needs to apply tone shift.
                auto expForVocoder = InterpreterCommon::TensorHelper<float>::createFor1DArray(targetLength);
                if (!expForVocoder) {
                    setState(Failed);
                    return expForVocoder.takeError();
                }
                auto &vocoderHelper = expForVocoder.value();
                // Convert midi note to hz
                for (size_t i = 0; i < targetLength; ++i) {
                    // toneShiftSamples and pitchSamples are both of same size: targetLength.
                    // This has been checked in previous steps.
                    // Therefore, the buffer is guaranteed not to overflow.
                    auto midi_note = pitchSamples[i] + toneShiftSamples[i] / 100.0;
                    auto f0Vocoder = a4_freq_hz * std::exp2((midi_note - midi_a4_note) / 12.0);
                    vocoderHelper.writeUnchecked(static_cast<float>(f0Vocoder));
                }
                f0TensorForVocoder = vocoderHelper.take();
            }
            satisfyToneShift = true;
        } else {
            // No pitch found
            setState(Failed);
            return srt::Error(srt::Error::SessionError, "parameter pitch missing");
        }

        // Some parameter requirements are not satisfied
        if (!satisfyEnergy || !satisfyBreathiness || !satisfyVoicing || !satisfyTension) {
            setState(Failed);
            std::string msg = "some required parameters missing:";
            if (!satisfyEnergy)
                msg += R"( "energy")";
            if (!satisfyBreathiness)
                msg += R"( "breathiness")";
            if (!satisfyVoicing)
                msg += R"( "voicing")";
            if (!satisfyTension)
                msg += R"( "tension")";
            return srt::Error(srt::Error::SessionError, std::move(msg));
        }

        // Speaker embedding
        if (config->useSpeakerEmbedding) {
            if (acousticInput->speakers.empty()) {
                setState(Failed);
                return srt::Error(srt::Error::SessionError, "no speakers found in acoustic input");
            }

            std::vector<int64_t> shape = {1, targetLength, Co::SpeakerEmbedding::Dimension};
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
                for (const auto &speaker : std::as_const(acousticInput->speakers)) {
                    if (auto it_speaker = config->speakers.find(speaker.name);
                        it_speaker != config->speakers.end()) {
                        const auto &embedding = it_speaker->second;
                        auto resampled = InterpreterCommon::resample(
                            speaker.proportions, speaker.interval, frameLength, targetLength, true);
                        for (size_t i = 0; i < resampled.size(); ++i) {
                            for (size_t j = 0; j < embedding.size(); ++j) {
                                float &val = buffer[i * embedding.size() + j];
                                val =
                                    std::fmaf(static_cast<float>(resampled[i]), embedding[j], val);
                            }
                        }
                    } else {
                        setState(Failed);
                        return srt::Error(srt::Error::InvalidArgument,
                                          "invalid speaker name: " + speaker.name);
                    }
                }
                sessionInput->inputs["spk_embed"] = tensor;
            } else {
                setState(Failed);
                return exp.takeError();
            }
        }

        constexpr const char *outParamMel = "mel";
        sessionInput->outputs.emplace(outParamMel);

        std::unique_lock<std::shared_mutex> lock(impl.mutex);
        impl.session = impl.driver->createSession();
        auto sessionOpenArgs = srt::NO<Onnx::SessionOpenArgs>::create();
        sessionOpenArgs->useCpu = false;
        if (auto res = impl.session->open(config->model, sessionOpenArgs); !res) {
            setState(Failed);
            return res;
        }
        auto sessionExp = impl.session->start(sessionInput);
        if (!sessionExp) {
            setState(Failed);
            return sessionExp.takeError();
        }

        impl.result = srt::NO<Ac::AcousticResult>::create();

        // Get session results
        auto result = impl.session->result();
        if (result->objectName() != Onnx::API_NAME) {
            setState(Failed);
            return srt::Error(srt::Error::InvalidArgument, "invalid result API name");
        }
        auto sessionResult = result.as<Onnx::SessionResult>();
        if (auto it_mel = sessionResult->outputs.find(outParamMel);
            it_mel != sessionResult->outputs.end()) {
            impl.result->mel = it_mel->second;
        } else {
            setState(Failed);
            return srt::Error(srt::Error::SessionError, "invalid result output");
        }
        impl.result->f0 = f0TensorForVocoder;

        lock.unlock();
        setState(Idle);
        return srt::Expected<void>();
    }

    srt::Expected<void> AcousticInference::startAsync(const srt::NO<srt::TaskStartInput> &input,
                                                      const StartAsyncCallback &callback) {
        // TODO:
        return srt::Error(srt::Error::NotImplemented);
    }

    bool AcousticInference::stop() {
        __stdc_impl_t;
        std::unique_lock<std::shared_mutex> lock(impl.mutex);
        if (!impl.session->isOpen()) {
            return false;
        }
        if (!impl.session->stop()) {
            return false;
        }
        setState(Terminated);
        return true;
    }

    srt::NO<srt::TaskResult> AcousticInference::result() const {
        __stdc_impl_t;
        std::shared_lock<std::shared_mutex> lock(impl.mutex);
        return impl.result;
    }

}