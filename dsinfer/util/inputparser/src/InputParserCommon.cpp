#include "InputParserCommon_p.h"

#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <stdcorelib/str.h>

namespace ds {

    namespace Co = Api::Common::L1;

    srt::Expected<void> parseOptionalNumber(const srt::JsonObject &obj, std::string_view fieldName,
                                            double &out) {
        const std::string field(fieldName);
        const auto it = obj.find(field);
        if (it == obj.end()) {
            return {};
        }
        if (!it->second.isNumber()) {
            return srt::Error(srt::Error::InvalidFormat, field + " must be a number");
        }
        out = it->second.toDouble();
        return {};
    }

    srt::Expected<void> parseOptionalInteger(const srt::JsonObject &obj, std::string_view fieldName,
                                             int64_t &out) {
        const std::string field(fieldName);
        const auto it = obj.find(field);
        if (it == obj.end()) {
            return {};
        }
        if (!it->second.isNumber()) {
            return srt::Error(srt::Error::InvalidFormat, field + " must be a number");
        }
        out = it->second.toInt();
        return {};
    }

    static inline std::optional<std::tuple<int, int, bool>>
        parseLibrosaPitch(const std::string &text) {
        const auto size = static_cast<int>(text.size());
        int begin = 0;
        int end = size - 1;

        while (begin < size && stdc::str::is_space(text[begin])) {
            ++begin;
        }
        while (end >= 0 && stdc::str::is_space(text[end])) {
            --end;
        }
        if (begin > end) {
            return std::nullopt;
        }

        const auto length = end - begin + 1;
        if (length == 4 && stdc::str::to_upper(text[begin]) == 'R' &&
            stdc::str::to_upper(text[begin + 1]) == 'E' &&
            stdc::str::to_upper(text[begin + 2]) == 'S' &&
            stdc::str::to_upper(text[begin + 3]) == 'T') {
            return std::tuple{0, 0, true};
        }

        const auto noteName = stdc::str::to_upper(text[begin]);
        if (noteName < 'A' || noteName > 'G') {
            return std::nullopt;
        }

        static constexpr std::array<int, 7> noteOffsets{9, 11, 0, 2, 4, 5, 7};
        const auto noteIndex = static_cast<size_t>((noteName - 'A' + 7) % 7);
        int semitone = noteOffsets[noteIndex];
        ++begin;

        while (begin <= end && (text[begin] == '#' || text[begin] == 'b')) {
            semitone += text[begin] == '#' ? 1 : -1;
            ++begin;
        }
        semitone = (semitone % 12 + 12) % 12;

        if (begin > end || !stdc::str::is_digit(text[begin])) {
            return std::nullopt;
        }
        int octave = 0;
        while (begin <= end && stdc::str::is_digit(text[begin])) {
            const auto digit = text[begin] - '0';
            if (octave > (std::numeric_limits<int>::max() - digit) / 10) {
                return std::nullopt;
            }
            octave = octave * 10 + digit;
            ++begin;
        }

        if (begin > end || (text[begin] != '+' && text[begin] != '-')) {
            return std::nullopt;
        }
        const auto sign = text[begin] == '-' ? -1 : 1;
        ++begin;

        if (begin > end || !stdc::str::is_digit(text[begin])) {
            return std::nullopt;
        }
        int cents = 0;
        while (begin <= end && stdc::str::is_digit(text[begin])) {
            const auto digit = text[begin] - '0';
            if (cents > (std::numeric_limits<int>::max() - digit) / 10) {
                return std::nullopt;
            }
            cents = cents * 10 + digit;
            ++begin;
        }
        cents *= sign;

        const auto maximumOctave = (std::numeric_limits<int>::max() - semitone) / 12 - 1;
        if (begin <= end || octave > maximumOctave) {
            return std::nullopt;
        }

        const auto key = 12 * (octave + 1) + semitone;
        return std::tuple{key, cents, false};
    }

    srt::Expected<void> parseValueCurve(const srt::JsonObject &parameter,
                                        const std::string &paramName, double &outInterval,
                                        std::vector<double> &outValues) {
        outInterval = 0;
        outValues.clear();

        bool dynamic = false;
        if (const auto dynamicIt = parameter.find("dynamic"); dynamicIt != parameter.end()) {
            if (!dynamicIt->second.isBool()) {
                return srt::Error(srt::Error::InvalidFormat,
                                  paramName + "[].dynamic must be a bool");
            }
            dynamic = dynamicIt->second.toBool();
        }

        if (!dynamic) {
            const auto valueIt = parameter.find("value");
            if (valueIt == parameter.end() || !valueIt->second.isNumber()) {
                return srt::Error(srt::Error::InvalidFormat,
                                  paramName + "[].value must exist and be a number");
            }
            outValues.push_back(valueIt->second.toDouble());
            return {};
        }

        const auto valuesIt = parameter.find("values");
        if (valuesIt == parameter.end() || !valuesIt->second.isArray()) {
            return srt::Error(srt::Error::InvalidFormat,
                              paramName + "[].values must exist and be an array of numbers");
        }
        const auto &values = valuesIt->second.toArray();
        std::vector<double> parsedValues;
        parsedValues.reserve(values.size());
        for (const auto &value : values) {
            if (!value.isNumber()) {
                return srt::Error(srt::Error::InvalidFormat,
                                  paramName + "[].values[] must be a number");
            }
            parsedValues.push_back(value.toDouble());
        }

        const auto intervalIt = parameter.find("interval");
        if (intervalIt == parameter.end() || !intervalIt->second.isNumber()) {
            return srt::Error(srt::Error::InvalidFormat,
                              paramName + "[].interval must exist and be a positive number");
        }
        outInterval = intervalIt->second.toDouble();
        if (outInterval <= 0) {
            return srt::Error(srt::Error::InvalidFormat,
                              paramName + "[].interval must be a positive number");
        }
        outValues = std::move(parsedValues);
        return {};
    }

    static inline int extractCents(double value) {
        double integerPart;
        const auto fraction = std::modf(value, &integerPart);
        return static_cast<int>(std::round(fraction * 100));
    }

    static srt::Expected<Co::InputPhonemeInfo::Speaker>
        parsePhonemeSpeaker(const srt::JsonValue &value) {
        if (!value.isObject()) {
            return srt::Error(srt::Error::InvalidFormat,
                              "words[].phones[].speakers[] must be an object");
        }

        const auto &obj = value.toObject();
        const auto nameIt = obj.find("name");
        if (nameIt == obj.end() || !nameIt->second.isString()) {
            return srt::Error(srt::Error::InvalidFormat,
                              "words[].phones[].speakers[].name must be a string");
        }

        Co::InputPhonemeInfo::Speaker result;
        result.name = nameIt->second.toString();
        if (const auto proportionIt = obj.find("proportion"); proportionIt != obj.end()) {
            if (!proportionIt->second.isNumber()) {
                return srt::Error(srt::Error::InvalidFormat,
                                  "words[].phones[].speakers[].proportion must be a number");
            }
            result.proportion = proportionIt->second.toDouble();
        }
        return result;
    }

    static srt::Expected<Co::InputPhonemeInfo> parsePhoneme(const srt::JsonValue &value) {
        if (!value.isObject()) {
            return srt::Error(srt::Error::InvalidFormat, "words[].phones[] must be an object");
        }

        const auto &obj = value.toObject();
        Co::InputPhonemeInfo result;
        if (const auto tokenIt = obj.find("token"); tokenIt != obj.end()) {
            if (!tokenIt->second.isString()) {
                return srt::Error(srt::Error::InvalidFormat,
                                  "words[].phones[].token must be a string");
            }
            result.token = tokenIt->second.toString();
        }
        if (const auto languageIt = obj.find("language"); languageIt != obj.end()) {
            if (!languageIt->second.isString()) {
                return srt::Error(srt::Error::InvalidFormat,
                                  "words[].phones[].language must be a string");
            }
            result.language = languageIt->second.toString();
        }
        if (const auto toneIt = obj.find("tone"); toneIt != obj.end()) {
            if (!toneIt->second.isInt()) {
                return srt::Error(srt::Error::InvalidFormat,
                                  "words[].phones[].tone must be an integer");
            }
            result.tone = static_cast<int>(toneIt->second.toInt());
        }
        if (const auto startIt = obj.find("start"); startIt != obj.end()) {
            if (!startIt->second.isNumber()) {
                return srt::Error(srt::Error::InvalidFormat,
                                  "words[].phones[].start must be a number");
            }
            result.start = startIt->second.toDouble();
        }
        if (const auto speakersIt = obj.find("speakers"); speakersIt != obj.end()) {
            if (!speakersIt->second.isArray()) {
                return srt::Error(srt::Error::InvalidFormat,
                                  "words[].phones[].speakers must be an array");
            }
            const auto &speakers = speakersIt->second.toArray();
            result.speakers.reserve(speakers.size());
            for (const auto &speaker : speakers) {
                auto parsed = parsePhonemeSpeaker(speaker);
                if (!parsed) {
                    return parsed.takeError();
                }
                result.speakers.push_back(parsed.take());
            }
        }
        return result;
    }

    static srt::Expected<Co::InputNoteInfo> parseNote(const srt::JsonValue &value) {
        if (!value.isObject()) {
            return srt::Error(srt::Error::InvalidFormat, "words[].notes[] must be an object");
        }

        const auto &obj = value.toObject();
        Co::InputNoteInfo result;
        if (const auto keyIt = obj.find("key"); keyIt != obj.end()) {
            if (keyIt->second.isInt()) {
                const auto key = keyIt->second.toInt();
                if (key < std::numeric_limits<int>::min() ||
                    key > std::numeric_limits<int>::max()) {
                    return srt::Error(srt::Error::InvalidFormat,
                                      "words[].notes[].key is out of range");
                }
                result.key = static_cast<int>(key);
            } else if (keyIt->second.isDouble()) {
                const auto key = keyIt->second.toDouble();
                if (!std::isfinite(key) || key < std::numeric_limits<int>::min() ||
                    key > std::numeric_limits<int>::max()) {
                    return srt::Error(srt::Error::InvalidFormat,
                                      "words[].notes[].key is out of range");
                }
                result.key = static_cast<int>(key);
                result.cents = extractCents(key);
            } else if (keyIt->second.isString()) {
                const auto parsed = parseLibrosaPitch(keyIt->second.toString());
                if (!parsed) {
                    return srt::Error(srt::Error::InvalidFormat,
                                      "words[].notes[].key has an invalid format");
                }
                std::tie(result.key, result.cents, result.is_rest) = *parsed;
            } else {
                return srt::Error(srt::Error::InvalidFormat,
                                  "words[].notes[].key must be a number or string");
            }
        }

        if (const auto centsIt = obj.find("cents"); centsIt != obj.end()) {
            if (!centsIt->second.isNumber()) {
                return srt::Error(srt::Error::InvalidFormat,
                                  "words[].notes[].cents must be a number");
            }
            const auto cents = centsIt->second.toDouble();
            if (!std::isfinite(cents) || cents < std::numeric_limits<int>::min() ||
                cents > std::numeric_limits<int>::max()) {
                return srt::Error(srt::Error::InvalidFormat,
                                  "words[].notes[].cents is out of range");
            }
            const auto combinedCents =
                static_cast<int64_t>(result.cents) + static_cast<int64_t>(std::round(cents));
            if (combinedCents < std::numeric_limits<int>::min() ||
                combinedCents > std::numeric_limits<int>::max()) {
                return srt::Error(srt::Error::InvalidFormat,
                                  "words[].notes[].cents is out of range");
            }
            result.cents = static_cast<int>(combinedCents);
        }
        if (const auto durationIt = obj.find("duration"); durationIt != obj.end()) {
            if (!durationIt->second.isNumber()) {
                return srt::Error(srt::Error::InvalidFormat,
                                  "words[].notes[].duration must be a number");
            }
            result.duration = durationIt->second.toDouble();
        }
        if (const auto glideIt = obj.find("glide"); glideIt != obj.end()) {
            if (!glideIt->second.isString()) {
                return srt::Error(srt::Error::InvalidFormat,
                                  "words[].notes[].glide must be a string");
            }
            const auto glide = stdc::to_lower(glideIt->second.toString());
            if (glide == "up") {
                result.glide = Co::GlideType::Up;
            } else if (glide == "down") {
                result.glide = Co::GlideType::Down;
            } else if (glide != "none") {
                return srt::Error(srt::Error::InvalidFormat,
                                  R"(words[].notes[].glide must be "up", "down", or "none")");
            }
        }
        if (const auto restIt = obj.find("is_rest"); restIt != obj.end()) {
            if (!restIt->second.isBool()) {
                return srt::Error(srt::Error::InvalidFormat,
                                  "words[].notes[].is_rest must be a bool");
            }
            result.is_rest = restIt->second.toBool();
        }

        const auto shiftedCents = static_cast<int64_t>(result.cents) + 50;
        const auto keyOffset =
            shiftedCents >= 0 ? shiftedCents / 100 : -((-shiftedCents + 99) / 100);
        const auto normalizedKey = static_cast<int64_t>(result.key) + keyOffset;
        if (normalizedKey < std::numeric_limits<int>::min() ||
            normalizedKey > std::numeric_limits<int>::max()) {
            return srt::Error(srt::Error::InvalidFormat,
                              "words[].notes[].key is out of range after cents normalization");
        }
        result.key = static_cast<int>(normalizedKey);
        result.cents -= static_cast<int>(keyOffset * 100);
        return result;
    }

    static srt::Expected<Co::InputWordInfo> parseWord(const srt::JsonValue &value) {
        if (!value.isObject()) {
            return srt::Error(srt::Error::InvalidFormat, "words[] must be an object");
        }

        const auto &obj = value.toObject();
        Co::InputWordInfo result;
        if (const auto phonesIt = obj.find("phones"); phonesIt != obj.end()) {
            if (!phonesIt->second.isArray()) {
                return srt::Error(srt::Error::InvalidFormat, "words[].phones must be an array");
            }
            const auto &phones = phonesIt->second.toArray();
            result.phones.reserve(phones.size());
            for (const auto &phone : phones) {
                auto parsed = parsePhoneme(phone);
                if (!parsed) {
                    return parsed.takeError();
                }
                result.phones.push_back(parsed.take());
            }
        }
        if (const auto notesIt = obj.find("notes"); notesIt != obj.end()) {
            if (!notesIt->second.isArray()) {
                return srt::Error(srt::Error::InvalidFormat, "words[].notes must be an array");
            }
            const auto &notes = notesIt->second.toArray();
            result.notes.reserve(notes.size());
            for (const auto &note : notes) {
                auto parsed = parseNote(note);
                if (!parsed) {
                    return parsed.takeError();
                }
                result.notes.push_back(parsed.take());
            }
        }
        return result;
    }

    srt::Expected<void> parseWords(const srt::JsonObject &obj,
                                   std::vector<Co::InputWordInfo> &outWords) {
        const auto wordsIt = obj.find("words");
        if (wordsIt == obj.end()) {
            return {};
        }
        if (!wordsIt->second.isArray()) {
            return srt::Error(srt::Error::InvalidFormat, "words must be an array");
        }

        const auto &words = wordsIt->second.toArray();
        std::vector<Co::InputWordInfo> parsedWords;
        parsedWords.reserve(words.size());
        for (const auto &word : words) {
            auto parsed = parseWord(word);
            if (!parsed) {
                return parsed.takeError();
            }
            parsedWords.push_back(parsed.take());
        }
        outWords = std::move(parsedWords);
        return {};
    }

    srt::Expected<void> parseParameters(const srt::JsonObject &obj, bool pitchOnly,
                                        std::vector<Co::InputParameterInfo> &outParameters) {
        const auto parametersIt = obj.find("parameters");
        if (parametersIt == obj.end()) {
            return {};
        }
        if (!parametersIt->second.isArray()) {
            return srt::Error(srt::Error::InvalidFormat, "parameters must be an array");
        }

        const auto &parameters = parametersIt->second.toArray();
        std::vector<Co::InputParameterInfo> parsedParameters;
        parsedParameters.reserve(parameters.size());
        for (const auto &value : parameters) {
            if (!value.isObject()) {
                return srt::Error(srt::Error::InvalidFormat, "parameters[] must be an object");
            }
            const auto &parameter = value.toObject();
            const auto tagIt = parameter.find("tag");
            if (tagIt == parameter.end() || !tagIt->second.isString()) {
                return srt::Error(srt::Error::InvalidFormat, "parameters[].tag must be a string");
            }

            const auto tag = stdc::to_lower(tagIt->second.toString());
            auto parameterInfo = [&]() -> Co::InputParameterInfo {
                if (tag == Co::Tags::Pitch.name()) {
                    return {Co::Tags::Pitch};
                }
                if (tag == Co::Tags::Breathiness.name()) {
                    return {Co::Tags::Breathiness};
                }
                if (tag == Co::Tags::Energy.name()) {
                    return {Co::Tags::Energy};
                }
                if (tag == Co::Tags::Gender.name()) {
                    return {Co::Tags::Gender};
                }
                if (tag == Co::Tags::Tension.name()) {
                    return {Co::Tags::Tension};
                }
                if (tag == Co::Tags::Velocity.name()) {
                    return {Co::Tags::Velocity};
                }
                if (tag == Co::Tags::Voicing.name()) {
                    return {Co::Tags::Voicing};
                }
                if (tag == Co::Tags::MouthOpening.name()) {
                    return {Co::Tags::MouthOpening};
                }
                if (tag == Co::Tags::ToneShift.name()) {
                    return {Co::Tags::ToneShift};
                }
                if (tag == Co::Tags::F0.name()) {
                    return {Co::Tags::F0};
                }
                if (tag == Co::Tags::Expr.name()) {
                    return {Co::Tags::Expr};
                }
                return {};
            }();

            if (parameterInfo.tag.name().empty()) {
                continue;
            }
            if (pitchOnly && parameterInfo.tag != Co::Tags::Pitch &&
                parameterInfo.tag != Co::Tags::Expr) {
                continue;
            }

            if (auto result = parseValueCurve(parameter, "parameters", parameterInfo.interval,
                                              parameterInfo.values);
                !result) {
                return result.takeError();
            }

            if (const auto retakeIt = parameter.find("retake"); retakeIt != parameter.end()) {
                if (!retakeIt->second.isObject()) {
                    return srt::Error(srt::Error::InvalidFormat,
                                      "parameters[].retake must be an object");
                }
                const auto &retake = retakeIt->second.toObject();
                const auto startIt = retake.find("start");
                const auto endIt = retake.find("end");
                if (startIt == retake.end() || !startIt->second.isNumber() ||
                    endIt == retake.end() || !endIt->second.isNumber()) {
                    return srt::Error(
                        srt::Error::InvalidFormat,
                        "parameters[].retake must contain numeric start and end fields");
                }
                parameterInfo.retake = Co::InputParameterInfo::RetakeRange{
                    startIt->second.toDouble(), endIt->second.toDouble()};
            }
            parsedParameters.push_back(std::move(parameterInfo));
        }

        outParameters = std::move(parsedParameters);
        return {};
    }

    srt::Expected<void> parseSpeakers(const srt::JsonObject &obj,
                                      std::vector<Co::InputSpeakerInfo> &outSpeakers) {
        const auto speakersIt = obj.find("speakers");
        if (speakersIt == obj.end()) {
            return {};
        }
        if (!speakersIt->second.isArray()) {
            return srt::Error(srt::Error::InvalidFormat, "speakers must be an array");
        }

        const auto &speakers = speakersIt->second.toArray();
        std::vector<Co::InputSpeakerInfo> parsedSpeakers;
        parsedSpeakers.reserve(speakers.size());
        for (const auto &value : speakers) {
            if (!value.isObject()) {
                return srt::Error(srt::Error::InvalidFormat, "speakers[] must be an object");
            }
            const auto &speaker = value.toObject();
            const auto nameIt = speaker.find("name");
            if (nameIt == speaker.end() || !nameIt->second.isString()) {
                return srt::Error(srt::Error::InvalidFormat, "speakers[].name must be a string");
            }

            Co::InputSpeakerInfo speakerInfo;
            speakerInfo.name = nameIt->second.toString();
            if (auto result = parseValueCurve(speaker, "speakers", speakerInfo.interval,
                                              speakerInfo.proportions);
                !result) {
                return result.takeError();
            }
            parsedSpeakers.push_back(std::move(speakerInfo));
        }

        outSpeakers = std::move(parsedSpeakers);
        return {};
    }

}
