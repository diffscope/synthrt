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

#include "internal/Interpolator.h"
#include "internal/TensorHelper.h"

namespace ds {

    namespace Co = Api::Common::L1;
    namespace Ac = Api::Acoustic::L1;
    namespace Onnx = Api::Onnx;
    namespace DiffSinger = Api::DiffSinger::L1;

    template <class T>
    std::vector<T> arange(T start, T stop, T step) {
        if ((stop < start) && (step > 0)) {
            return {};
        }
        auto size = static_cast<size_t>(std::ceil((stop - start) / step));
        if (size == 0) {
            return {};
        }

        std::vector<T> result;
        result.reserve(size);

        for (size_t i = 0; i < size; ++i) {
            result.push_back(start + i * step);
        }

        return result;
    }

    static inline std::vector<double> resample(const std::vector<double> &samples, double timestep,
                                               double targetTimestep, int64_t targetLength,
                                               bool fillLast) {
        if (samples.empty() || targetLength == 0) {
            return {};
        }
        if (samples.size() == 1) {
            std::vector<double> result(targetLength, samples[0]);
            return result;
        }
        if (timestep == 0 || targetTimestep == 0) {
            return {};
        }
        if (targetLength == 1) {
            return {samples[0]};
        }
        // Find the time duration of input samples in seconds.
        auto tMax = static_cast<double>(samples.size() - 1) * timestep;

        // Construct target time axis for interpolation.
        auto targetTimeAxis = arange(0.0, tMax, targetTimestep);

        // Construct input time axis (for interpolation).
        auto inputTimeAxis = arange(0.0, static_cast<double>(samples.size()), 1.0);
        std::transform(inputTimeAxis.begin(), inputTimeAxis.end(), inputTimeAxis.begin(),
                       [timestep](double value) { return value * timestep; });

        // Interpolate sample curve to target time axis
        auto targetSamples =
            interpolate<InterpolateLinear, double>(targetTimeAxis, inputTimeAxis, samples);

        // Resize the interpolated curve vector to target length
        auto actualLength = static_cast<int64_t>(targetSamples.size());

        if (actualLength > targetLength) {
            // Truncate vector to target length
            targetSamples.resize(targetLength);
        } else if (actualLength < targetLength) {
            // Expand vector to target length, filling last value
            double tailFillValue = fillLast ? targetSamples.back() : 0;
            targetSamples.resize(targetLength, tailFillValue);
        }
        return targetSamples;
    }

    template <typename T>
    static inline srt::Expected<srt::NO<Tensor>>
        createTensorFilled(const std::vector<int64_t> &shape, T value) {
        auto exp = Tensor::create(tensor_traits<T>::data_type, shape);
        if (!exp) {
            return exp.takeError();
        }
        auto tensor = exp.take();
        auto dataPtr = tensor->template mutableData<T>();
        std::fill(dataPtr, dataPtr + tensor->elementCount(), value);
        return tensor;
    }

    static inline size_t getPhoneCount(const srt::NO<Ac::AcousticStartInput> &input) {
        assert(input != nullptr);

        size_t phoneCount = 0;
        for (const auto &word : input->words) {
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
        const srt::NO<Ac::AcousticStartInput> &input,
        const std::map<std::string, int> &name2token) {

        constexpr const char *SP_TOKEN = "SP";
        constexpr const char *AP_TOKEN = "AP";

        using TensorType = int64_t;
        auto phoneCount = getPhoneCount(input);
        auto exp = TensorHelper<TensorType>::createFor1DArray(phoneCount);
        if (!exp) {
            return exp.takeError();
        }
        auto &helper = exp.value();

        for (const auto &word : input->words) {
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
        const srt::NO<Ac::AcousticStartInput> &input, const std::map<std::string, int> &languages) {

        auto phoneCount = getPhoneCount(input);
        using TensorType = int64_t;
        auto exp = TensorHelper<TensorType>::createFor1DArray(phoneCount);
        if (!exp) {
            return exp.takeError();
        }
        auto &helper = exp.value();

        for (const auto &word : input->words) {
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
        const srt::NO<Ac::AcousticStartInput> &input, double frameLength,
        int64_t *outTargetLength = nullptr) {

        auto phoneCount = getPhoneCount(input);
        using TensorType = int64_t;
        auto exp = TensorHelper<TensorType>::createFor1DArray(phoneCount);
        if (!exp) {
            return exp.takeError();
        }
        auto &helper = exp.value();

        double phoneDurSum = 0.0;
        int64_t targetLength = 0;

        for (size_t currWordIndex = 0; currWordIndex < input->words.size(); ++currWordIndex) {
            const auto &word = input->words[currWordIndex];
            auto wordDuration = getWordDuration(word);

            for (size_t i = 0; i < word.phones.size(); ++i) {

                // durations
                {
                    bool currPhoneIsTheLastPhone = (i == word.phones.size() - 1);
                    auto currPhoneStart = phoneDurSum + word.phones[i].start;
                    auto nextPhoneStart =
                        phoneDurSum +
                        (currPhoneIsTheLastPhone ? wordDuration : word.phones[i + 1].start);
                    if (currPhoneIsTheLastPhone && (currWordIndex + 1 < input->words.size())) {
                        // If current word is not the last word
                        const auto &nextWord = input->words[currWordIndex + 1];
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

    inline srt::Expected<void> loadSpeakerEmbedding(const std::filesystem::path &path,
                                                    Co::SpeakerEmbedding::Vector &outBuffer) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return srt::Error(srt::Error::FileNotFound,
                              "Failed to open file: " + stdc::path::to_utf8(path));
        }

        constexpr auto byteSize = Co::SpeakerEmbedding::Dimension * sizeof(float);
        file.read(reinterpret_cast<char *>(outBuffer.data()), byteSize);

        if (!file) {
            return srt::Error(srt::Error::SessionError, "File read failed: " + path.string());
        }

        if (file.gcount() != byteSize) {
            return srt::Error(srt::Error::SessionError, "File size is not exactly " +
                                                            std::to_string(byteSize) +
                                                            " bytes: " + path.string());
        }

        return srt::Expected<void>();
    }

    class AcousticInference::Impl {
    public:
        srt::Expected<void> getDriver(AcousticInference *obj) {
            if (!driver) {
                auto inferenceCate = obj->spec()->SU()->category("inference");
                auto dsdriverObject = inferenceCate->getFirstObject("dsdriver");

                if (!dsdriverObject) {
                    return srt::Error(srt::Error::SessionError, "could not find dsdriver");
                }

                auto onnxDriver = dsdriverObject.as<InferenceDriver>();

                const auto arch = onnxDriver->arch();
                constexpr auto expectedArch = DiffSinger::API_NAME;
                const bool isArchMatch = arch == expectedArch;

                const auto backend = onnxDriver->backend();
                constexpr auto expectedBackend = Onnx::API_NAME;
                const bool isBackendMatch = backend == expectedBackend;

                if (!isArchMatch || !isBackendMatch) {
                    return srt::Error(
                        srt::Error::SessionError,
                        stdc::formatN(
                            R"(invalid driver: expected arch "%1", got "%2" (%3); expected backend "%4", got "%5" (%6))",
                            expectedArch, arch, (isArchMatch ? "match" : "MISMATCH"),
                            expectedBackend, backend, (isBackendMatch ? "match" : "MISMATCH")));
                }

                driver = std::move(onnxDriver);
            }
            return srt::Expected<void>();
        }

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

        if (auto res = impl.getDriver(this); !res) {
            setState(Failed);
            return res;
        }

        // Initialize inference state
        setState(Idle);

        // return success
        return srt::Expected<void>();
    }

    srt::Expected<void> AcousticInference::start(const srt::NO<srt::TaskStartInput> &input) {
        __stdc_impl_t;
        // TODO:
        if (auto res = impl.getDriver(this); !res) {
            setState(Failed);
            return res;
        }

        setState(Running); // 设置状态

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
        if (auto res = parsePhonemeTokens(acousticInput, config->phonemes); res) {
            sessionInput->inputs["tokens"] = res.take();
        } else {
            setState(Failed);
            return res.takeError();
        }

        // input param: languages
        if (config->useLanguageId) {
            if (auto res = parsePhonemeLanguages(acousticInput, config->languages); res) {
                sessionInput->inputs["languages"] = res.take();
            } else {
                setState(Failed);
                return res.takeError();
            }
        }

        // input param: durations
        int64_t targetLength;

        if (auto res = parsePhonemeDurations(acousticInput, frameLength, &targetLength); res) {
            sessionInput->inputs["durations"] = res.take();
        } else {
            setState(Failed);
            return res.takeError();
        }

        // input param: steps
        {
            auto exp = Tensor::createFromSingleValue(acousticInput->steps);
            if (!exp) {
                setState(Failed);
                return exp.takeError();
            }
            sessionInput->inputs["steps"] = exp.take();
        }

        // input param: depth
        {
            auto exp = Tensor::createFromSingleValue(acousticInput->depth);
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
            auto resampled =
                resample(param.values, param.interval, frameLength, targetLength, true);
            if (resampled.empty()) {
                // These parameters are optional
                if (param.tag == Co::Tags::Gender) {
                    // Fill gender with 0
                    auto exp =
                        createTensorFilled<float>(std::vector<int64_t>{1, targetLength}, 0.0f);
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
                        createTensorFilled<float>(std::vector<int64_t>{1, targetLength}, 1.0f);
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

            auto exp = TensorHelper<float>::createFor1DArray(targetLength);
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
            auto pitchSamples =
                resample(pitchParam.values, pitchParam.interval, frameLength, targetLength, true);
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
                toneShiftSamples = resample(toneShiftParam.values, toneShiftParam.interval,
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
            auto expForAcoustic = TensorHelper<float>::createFor1DArray(targetLength);
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
                auto expForVocoder = TensorHelper<float>::createFor1DArray(targetLength);
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

            std::map<std::string, Co::SpeakerEmbedding::Vector> speakerEmbeddingMapping;
            for (const auto &[speaker, path] : std::as_const(config->speakers)) {
                auto [it, _] = speakerEmbeddingMapping.emplace(speaker, Co::SpeakerEmbedding::Vector{});
                if (auto exp = loadSpeakerEmbedding(path, it->second); !exp) {
                    setState(Failed);
                    return exp.takeError();
                }
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
                    if (auto it_speaker = speakerEmbeddingMapping.find(speaker.name);
                        it_speaker != speakerEmbeddingMapping.end()) {
                        const auto &embedding = it_speaker->second;
                        auto resampled = resample(speaker.proportions, speaker.interval,
                                                  frameLength, targetLength, true);
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

        impl.result = srt::NO<Ac::AcousticResult>::create(); // 创建结果

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
        setState(Terminated); // 设置状态
        return true;
    }

    srt::NO<srt::TaskResult> AcousticInference::result() const {
        __stdc_impl_t;
        std::shared_lock<std::shared_mutex> lock(impl.mutex);
        return impl.result;
    }

}