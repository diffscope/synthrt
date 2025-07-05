#include <InterpreterCommon/LinguisticEncoder.h>

#include <cmath>
#include <cstddef>
#include <cstdint>

#include <stdcorelib/stdc_global.h>

#include <InterpreterCommon/TensorHelper.h>
#include <InterpreterCommon/InputWord.h>

namespace ds::InterpreterCommon {

    namespace Co = Api::Common::L1;

    srt::Expected<srt::NO<Api::Onnx::SessionStartInput>>
        preprocessLinguisticPhoneme(const std::vector<Api::Common::L1::InputWordInfo> &words,
                                    const std::map<std::string, int> &tokens,
                                    const std::map<std::string, int> &languages, bool useLanguageId,
                                    double frameWidth) {

        auto sessionInput = srt::NO<Api::Onnx::SessionStartInput>::create();

        if (auto exp = preprocessPhonemeTokens(words, tokens); exp) {
            sessionInput->inputs.emplace("tokens", exp.take());
        } else {
            return exp.takeError();
        }

        if (useLanguageId) {
            if (auto exp = preprocessPhonemeLanguages(words, languages); exp) {
                sessionInput->inputs.emplace("languages", exp.take());
            } else {
                return exp.takeError();
            }
        }

        if (auto exp = preprocessPhonemeDurations(words, frameWidth); exp) {
            sessionInput->inputs.emplace("ph_dur", exp.take());
        } else {
            return exp.takeError();
        }

        // session output names
        sessionInput->outputs = {"encoder_out", "x_masks"};

        return sessionInput;
    }

    srt::Expected<srt::NO<Api::Onnx::SessionStartInput>>
        preprocessLinguisticWord(const std::vector<Api::Common::L1::InputWordInfo> &words,
                                 const std::map<std::string, int> &tokens,
                                 const std::map<std::string, int> &languages, bool useLanguageId,
                                 double frameWidth) {

        auto sessionInput = srt::NO<Api::Onnx::SessionStartInput>::create();

        if (auto exp = preprocessPhonemeTokens(words, tokens); exp) {
            sessionInput->inputs.emplace("tokens", exp.take());
        } else {
            return exp.takeError();
        }

        if (useLanguageId) {
            if (auto exp = preprocessPhonemeLanguages(words, languages); exp) {
                sessionInput->inputs.emplace("languages", exp.take());
            } else {
                return exp.takeError();
            }
        }

        // word_div
        if (auto exp = TensorHelper<int64_t>::createFor1DArray(words.size()); exp) {
            auto &wordDiv = exp.value();
            for (const auto &word : words) {
                wordDiv.writeUnchecked(word.phones.size());
            }
            sessionInput->inputs.emplace("word_div", wordDiv.take());
        } else {
            return exp.takeError();
        }

        // word_dur
        if (auto exp = TensorHelper<int64_t>::createFor1DArray(words.size()); exp) {
            auto &wordDurFrames = exp.value();
            int64_t prevFrames = 0;
            double currDuration = 0.0;
            for (const auto &word : words) {
                currDuration += getWordDuration(word);
                int64_t currFrames = std::llround(currDuration / frameWidth);
                wordDurFrames.writeUnchecked(currFrames - prevFrames);
                prevFrames = currFrames;
            }
            sessionInput->inputs.emplace("word_dur", wordDurFrames.take());
        } else {
            return exp.takeError();
        }

        // session output names
        sessionInput->outputs = {"encoder_out", "x_masks"};

        return sessionInput;
    }
    srt::Expected<void> runEncoder(const srt::NO<InferenceSession> &encoderSession,
                                   const srt::NO<srt::TaskStartInput> &linguisticInput,
                                   srt::NO<Api::Onnx::SessionStartInput> &out,
                                   bool useXMasks) {
        // Assuming encoderSession is already opened
        auto sessionExp = encoderSession->start(linguisticInput);
        if (!sessionExp) {
            return sessionExp.takeError();
        }

        // Get encoder session results
        auto result = encoderSession->result();
        if (!result) {
            return srt::Error(srt::Error::SessionError,
                              "linguistic encoder session result is nullptr");
        }
        if (result->objectName() != Api::Onnx::API_NAME) {
            return srt::Error(srt::Error::InvalidArgument, "invalid result API name");
        }
        auto encoderResult = result.as<Api::Onnx::SessionResult>();
        for (auto &&[name, value] : encoderResult->outputs) {
            if (name == "encoder_out") {
                out->inputs.emplace("encoder_out", std::move(value));
            } else if (useXMasks && name == "x_masks") {
                out->inputs.emplace("x_masks", std::move(value));
            }
        }
        return srt::Expected<void>();
    }
}