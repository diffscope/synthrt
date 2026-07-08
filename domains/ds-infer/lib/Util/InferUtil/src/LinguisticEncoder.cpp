#include <inferutil/LinguisticEncoder.h>

#include <cmath>
#include <cstddef>
#include <cstdint>

#include <stdcorelib/stdc_global.h>

#include <inferutil/TensorHelper.h>
#include <inferutil/InputWord.h>

namespace ds::infer::inferutil {

    namespace Co = srt::svs::Api::Common::L1;

    srt::core::Expected<srt::core::NO<srt::driver::onnx::SessionStartInput>>
        preprocessLinguisticPhoneme(const std::vector<srt::svs::Api::Common::L1::InputWordInfo> &words,
                                    const std::map<std::string, int> &tokens,
                                    const std::map<std::string, int> &languages, bool useLanguageId,
                                    double frameWidth) {

        auto sessionInput = srt::core::NO<srt::driver::onnx::SessionStartInput>::create();

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

    srt::core::Expected<srt::core::NO<srt::driver::onnx::SessionStartInput>>
        preprocessLinguisticWord(const std::vector<srt::svs::Api::Common::L1::InputWordInfo> &words,
                                 const std::map<std::string, int> &tokens,
                                 const std::map<std::string, int> &languages, bool useLanguageId,
                                 double frameWidth) {

        auto sessionInput = srt::core::NO<srt::driver::onnx::SessionStartInput>::create();

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
    srt::core::Expected<void> runEncoder(const srt::core::NO<srt::driver::InferenceSession> &encoderSession,
                                   const srt::core::NO<srt::core::TaskStartInput> &linguisticInput,
                                   srt::core::NO<srt::driver::onnx::SessionStartInput> &out,
                                   bool useXMasks) {
        // Assuming encoderSession is already opened
        srt::core::NO<srt::core::TaskResult> sessionTaskResult;
        auto sessionExp = encoderSession->start(linguisticInput);
        if (!sessionExp) {
            return sessionExp.takeError();
        } else {
            sessionTaskResult = sessionExp.take();
        }

        // Get encoder session results
        if (!sessionTaskResult) {
            return srt::core::Error(srt::core::Error::SessionError,
                              "linguistic encoder session result is nullptr");
        }
        if (sessionTaskResult->objectName() != srt::driver::onnx::API_NAME) {
            return srt::core::Error(srt::core::Error::InvalidArgument, "invalid result API name");
        }
        auto encoderResult = sessionTaskResult.as<srt::driver::onnx::SessionResult>();
        for (auto &&[name, value] : encoderResult->outputs) {
            if (name == "encoder_out") {
                out->inputs.emplace("encoder_out", std::move(value));
            } else if (useXMasks && name == "x_masks") {
                out->inputs.emplace("x_masks", std::move(value));
            }
        }
        return srt::core::Expected<void>();
    }
}