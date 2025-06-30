#include "InputParserCommon_p.h"

#include <cctype>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include <stdcorelib/str.h>

namespace ds {

    namespace Co = Api::Common::L1;

    static inline std::optional<std::tuple<int, int, bool>> parseLibrosaPitch(const std::string &s) {
        int n = (int) s.size();
        int i = 0, j = n - 1;

        // 0. skip leading and trailing spaces
        while (i < n && std::isspace(static_cast<unsigned char>(s[i]))) {
            ++i;
        }
        while (j >= 0 && std::isspace(static_cast<unsigned char>(s[j]))) {
            --j;
        }
        if (i > j) {
            // spaces only
            return std::nullopt;
        }
        // extract trimmed substring range [i..j]
        int len = j - i + 1;

        // 1. check for rest (case-insensitive)
        if (len == 4 && (std::toupper(static_cast<unsigned char>(s[i])) == 'R') &&
            (std::toupper(static_cast<unsigned char>(s[i + 1])) == 'E') &&
            (std::toupper(static_cast<unsigned char>(s[i + 2])) == 'S') &&
            (std::toupper(static_cast<unsigned char>(s[i + 3])) == 'T')) {
            // rest
            return std::make_optional(std::make_tuple(0, 0, true));
        }

        // 2. read key name (case-insensitive)
        char c = std::toupper(static_cast<unsigned char>(s[i]));
        if (c < 'A' || c > 'G') {
            // invalid key name
            return std::nullopt;
        }

        // C=0, D=2, E=4, F=5, G=7, A=9, B=11
        static const int baseIdx[7] = {9, 11, 0, 2, 4, 5, 7};
        int letterIndex = (c - 'A' + 7) % 7;
        int noteIdx = baseIdx[letterIndex];
        ++i;

        // 3. any numbers of accidentals (sharps # or flats b)
        while (i <= j && (s[i] == '#' || s[i] == 'b')) {
            noteIdx += (s[i] == '#') ? +1 : -1;
            ++i;
        }
        noteIdx = (noteIdx % 12 + 12) % 12;

        // 4. octave (non-negative, multi digits)
        if (i > j || !std::isdigit(static_cast<unsigned char>(s[i])))
            return std::nullopt;
        int octave = 0;
        while (i <= j && std::isdigit(static_cast<unsigned char>(s[i]))) {
            octave = octave * 10 + (s[i] - '0');
            ++i;
        }

        // 5. cents sign (+ or -)
        if (i > j || (s[i] != '+' && s[i] != '-'))
            return std::nullopt;
        int sign = (s[i] == '-') ? -1 : +1;
        ++i;

        // 6. cents number
        if (i > j || !std::isdigit(static_cast<unsigned char>(s[i])))
            return std::nullopt;
        int cents = 0;
        while (i <= j && std::isdigit(static_cast<unsigned char>(s[i]))) {
            cents = cents * 10 + (s[i] - '0');
            ++i;
        }
        cents *= sign;

        // 7. no redundant characters
        if (i <= j) {
            // redundant characters detected
            return std::nullopt;
        }

        // 8. calculate MIDI key
        int key = 12 * (octave + 1) + noteIdx;
        return std::make_optional(std::make_tuple(key, cents, false));
    }

    srt::Expected<void> parseValueCurve(const srt::JsonObject &parameter,
                                        const std::string &paramName, double &outInterval,
                                        std::vector<double> &outValues) {
        // parameters[].dynamic optional field, default to false
        bool isDynamic = false;
        if (auto it_dynamic = parameter.find("dynamic"); it_dynamic != parameter.end()) {
            if (!it_dynamic->second.isBool()) {
                return srt::Error(srt::Error::InvalidFormat,
                                  paramName + "[].dynamic must be a bool");
            }
            isDynamic = it_dynamic->second.toBool();
        }
        if (isDynamic) {
            // if dynamic, then look for `values` array of numbers and `interval`
            if (auto it_value = parameter.find("values"); it_value != parameter.end()) {
                // for dynamic parameter, populate values array from JSON input
                if (!it_value->second.isArray()) {
                    return srt::Error(srt::Error::InvalidFormat,
                                      "for dynamic parameter, " + paramName +
                                          "[].values must be an array of numbers");
                }
                const auto &values = it_value->second.toArray();
                outValues.reserve(values.size());
                for (const auto &value : values) {
                    if (!value.isNumber()) {
                        return srt::Error(srt::Error::InvalidFormat,
                                          "for dynamic parameter, " + paramName +
                                              "[].values[] item must be a number");
                    }
                    outValues.push_back(value.toDouble());
                }
                // for dynamic parameter, `interval` must exist and be a positive number
                if (auto it_interval = parameter.find("interval"); it_interval != parameter.end()) {
                    if (!it_interval->second.isNumber()) {
                        return srt::Error(srt::Error::InvalidFormat,
                                          "for dynamic parameter, " + paramName +
                                              "[].interval must be a positive number");
                    }
                    outInterval = it_interval->second.toDouble();
                    if (outInterval <= 0) {
                        return srt::Error(srt::Error::InvalidFormat,
                                          "for dynamic parameter, " + paramName +
                                              "[].interval must be a positive number");
                    }
                } else {
                    return srt::Error(srt::Error::InvalidFormat,
                                      "for dynamic parameter, " + paramName +
                                          "[].interval must exist and be a positive number");
                } // parameters[].interval
            } else {
                return srt::Error(srt::Error::InvalidFormat,
                                  "for dynamic parameter, " + paramName +
                                      "[].values must be exist and be an array of numbers");
            } // parameters[].values
        } else {
            // if not dynamic, then look for single `value`
            if (auto it_value = parameter.find("value"); it_value != parameter.end()) {
                if (!it_value->second.isNumber()) {
                    return srt::Error(srt::Error::InvalidFormat, "for non-dynamic parameter, " +
                                                                     paramName +
                                                                     "[].value must be a number");
                }
                outValues.push_back(it_value->second.toDouble());
            } else {
                return srt::Error(srt::Error::InvalidFormat,
                                  "for non-dynamic parameter, " + paramName +
                                      "[].value must be exist and be a number");
            }
            outInterval = 0; // 0 is reserved for non-dynamic parameter
        } // isDynamic
        return srt::Expected<void>();
    }

    static inline int extractCents(double value) {
        // XX.YYZZZZZZZ -> YY
        double integerPart;
        double decimalAfter100 = std::modf(value * 100.0, &integerPart);

        int digitAfterCents = static_cast<int>((decimalAfter100 * 10.0) + 1e-8);

        int cents = static_cast<int>(integerPart);
        if (digitAfterCents >= 5) {
            ++cents;
        }

        if (cents >= 100) {
            cents = 99;
        } else if (cents < 0) {
            cents = 0;
        }

        return cents;
    }

    srt::Expected<void> parseWords(const srt::JsonObject &obj,
                                   std::vector<Co::InputWordInfo> &outWords) {
        if (auto it_words = obj.find("words"); it_words != obj.end()) {
            if (!it_words->second.isArray()) {
                return srt::Error(srt::Error::InvalidFormat, "words must be an array");
            }
            const auto &words = it_words->second.toArray();
            outWords.reserve(words.size());
            for (const auto &maybeWord : words) {
                if (!maybeWord.isObject()) {
                    return srt::Error(srt::Error::InvalidFormat,
                                      "words[] array items must be an object");
                }
                const auto &word = maybeWord.toObject();
                Co::InputWordInfo wordInfo;
                if (auto it_phones = word.find("phones"); it_phones != word.end()) {
                    if (!it_phones->second.isArray()) {
                        return srt::Error(srt::Error::InvalidFormat,
                                          "words[].phones must be an array");
                    }
                    const auto &phones = it_phones->second.toArray();
                    wordInfo.phones.reserve(phones.size());
                    for (const auto &maybePhone : phones) {
                        if (!maybePhone.isObject()) {
                            return srt::Error(srt::Error::InvalidFormat,
                                              "words[].phones[] array items must be an object");
                        }
                        const auto &phone = maybePhone.toObject();

                        Co::InputPhonemeInfo phoneInfo;
                        if (auto it_token = phone.find("token"); it_token != phone.end()) {
                            if (!it_token->second.isString()) {
                                return srt::Error(srt::Error::InvalidFormat,
                                                  "words[].phones[].token must be a string");
                            }
                            phoneInfo.token = it_token->second.toString();
                        } // words[].phones[].token
                        if (auto it_language = phone.find("language"); it_language != phone.end()) {
                            if (!it_language->second.isString()) {
                                return srt::Error(srt::Error::InvalidFormat,
                                                  "words[].phones[].language must be a string");
                            }
                            phoneInfo.language = it_language->second.toString();
                        } // words[].phones[].language
                        if (auto it_tone = phone.find("tone"); it_tone != phone.end()) {
                            if (!it_tone->second.isInt()) {
                                return srt::Error(srt::Error::InvalidFormat,
                                                  "words[].phones[].tone must be an integer");
                            }
                            phoneInfo.tone = static_cast<int>(it_tone->second.toInt());
                        } // words[].phones[].tone
                        if (auto it_start = phone.find("start"); it_start != phone.end()) {
                            if (!it_start->second.isNumber()) {
                                return srt::Error(srt::Error::InvalidFormat,
                                                  "words[].phones[].start must be a number");
                            }
                            phoneInfo.start = it_start->second.toDouble();
                        } // words[].phones[].start
                        if (auto it_speakers = phone.find("speakers"); it_speakers != phone.end()) {
                            if (!it_speakers->second.isArray()) {
                                return srt::Error(srt::Error::InvalidFormat,
                                                  "words[].phones[].speakers must be an array");
                            }
                            const auto &speakers = it_speakers->second.toArray();
                            phoneInfo.speakers.reserve(speakers.size());
                            for (const auto &maybeSpeaker : speakers) {
                                if (!maybeSpeaker.isObject()) {
                                    return srt::Error(
                                        srt::Error::InvalidFormat,
                                        "words[].phones[].speakers must be an object");
                                }
                                const auto &speaker = maybeSpeaker.toObject();
                                Co::InputPhonemeInfo::Speaker speakerInfo;
                                auto it_speaker_name = speaker.find("name");
                                if (it_speaker_name == speaker.end() ||
                                    !it_speaker_name->second.isString()) {
                                    return srt::Error(
                                        srt::Error::InvalidFormat,
                                        R"(words[].phones[].speakers object must contain "name" (string) field)");
                                }
                                speakerInfo.name = it_speaker_name->second.toString();

                                auto it_speaker_proportion = speaker.find("proportion");
                                speakerInfo.proportion =
                                    (it_speaker_proportion == speaker.end())
                                        ? 1
                                        : it_speaker_proportion->second.toDouble(1);

                                phoneInfo.speakers.push_back(std::move(speakerInfo));
                            }
                        } // words[].phones[].speakers
                        wordInfo.phones.push_back(std::move(phoneInfo));
                    } // words[].phones[] <ITER>
                } // words[].phones
                if (auto it_notes = word.find("notes"); it_notes != word.end()) {
                    if (!it_notes->second.isArray()) {
                        return srt::Error(srt::Error::InvalidFormat,
                                          "words[].notes must be an array");
                    }
                    const auto &notes = it_notes->second.toArray();
                    wordInfo.notes.reserve(notes.size());
                    for (const auto &maybeNote : notes) {
                        if (!maybeNote.isObject()) {
                            return srt::Error(srt::Error::InvalidFormat,
                                              "words[].notes[] array items must be an object");
                        }
                        const auto &note = maybeNote.toObject();

                        Co::InputNoteInfo noteInfo;
                        if (auto it_key = note.find("key"); it_key != note.end()) {
                            // We accept 3 forms of keys:
                            // [1] Integer key: directly interpret as key
                            // [2] Float key: Convert to key and cents
                            // [3] String key: parse "C4-20" "D#4+25" (librosa form) as key and
                            // cents
                            if (it_key->second.isInt()) {
                                noteInfo.key = static_cast<int>(it_key->second.toInt());
                                noteInfo.cents = 0;
                            } else if (it_key->second.isDouble()) {
                                double doubleKey = it_key->second.toDouble();
                                noteInfo.key = static_cast<int>(doubleKey);
                                noteInfo.cents = extractCents(doubleKey);
                            } else if (it_key->second.isString()) {
                                // parse librosa-like key string
                                if (auto opt = parseLibrosaPitch(it_key->second.toString());
                                    opt.has_value()) {
                                    std::tie(noteInfo.key, noteInfo.cents, noteInfo.is_rest) =
                                        opt.value_or(std::make_tuple(0, 0, false));
                                } else {
                                    return srt::Error(srt::Error::InvalidFormat,
                                                      "words[].notes[].key invalid format");
                                }
                            }
                        } // words[].notes[].key
                        if (auto it_cents = note.find("cents"); it_cents != note.end()) {
                            if (!it_cents->second.isNumber()) {
                                return srt::Error(srt::Error::InvalidFormat,
                                                  "words[].notes[].cents must be a number");
                            }
                            if (it_cents->second.isInt()) {
                                // add cents to existing cents, maybe parsed from float key or
                                // librosa-like string key
                                noteInfo.cents += static_cast<int>(it_cents->second.toInt());
                            } else {
                                noteInfo.cents =
                                    static_cast<int>(std::round(it_cents->second.toDouble()));
                            }
                        } // words[].notes[].cents
                        if (auto it_duration = note.find("duration"); it_duration != note.end()) {
                            if (!it_duration->second.isNumber()) {
                                return srt::Error(srt::Error::InvalidFormat,
                                                  "words[].notes[].duration must be a number");
                            }
                            noteInfo.duration = it_duration->second.toDouble();
                        } // words[].notes[].duration
                        if (auto it_glide = note.find("glide"); it_glide != note.end()) {
                            if (!it_glide->second.isString()) {
                                return srt::Error(
                                    srt::Error::InvalidFormat,
                                    R"(words[].notes[].glide must be a string of following values: "up", "down", "none"])");
                            }
                            const auto glide = stdc::to_lower(it_glide->second.toString());
                            if (glide == "up") {
                                noteInfo.glide = Co::GlideType::GT_Up;
                            } else if (glide == "down") {
                                noteInfo.glide = Co::GlideType::GT_Down;
                            } else {
                                noteInfo.glide = Co::GlideType::GT_None;
                            }
                        } // words[].notes[].glide
                        if (auto it_is_rest = note.find("is_rest"); it_is_rest != note.end()) {
                            if (!it_is_rest->second.isBool()) {
                                return srt::Error(srt::Error::InvalidFormat,
                                                  "words[].notes[].is_rest must be a bool");
                            }
                            noteInfo.is_rest = it_is_rest->second.toBool();
                        } // words[].notes[].is_rest

                        // normalize `key` and `cents` such that `cents` lies in range [-50, 49]
                        {
                            int offset = (noteInfo.cents + 50) / 100;
                            noteInfo.key += offset;
                            noteInfo.cents -= offset * 100;
                        }
                        wordInfo.notes.push_back(noteInfo);
                    } // words[].notes[] <ITER>
                } // words.notes
                outWords.push_back(std::move(wordInfo));
            } // words[] <ITER>
        } // words
        return srt::Expected<void>();
    }

    srt::Expected<void> parseParameters(const srt::JsonObject &obj,
                                        std::vector<Co::InputParameterInfo> &outParameters) {
        if (auto it_parameters = obj.find("parameters"); it_parameters != obj.end()) {
            if (!it_parameters->second.isArray()) {
                return srt::Error(srt::Error::InvalidFormat, "parameters must be an array");
            }
            const auto &parameters = it_parameters->second.toArray();
            outParameters.reserve(parameters.size());
            for (const auto &maybeParameter : parameters) {
                if (!maybeParameter.isObject()) {
                    return srt::Error(srt::Error::InvalidFormat,
                                      "parameters[] array item must be an object");
                }
                const auto &parameter = maybeParameter.toObject();

                // <BEGIN> parsing parameters[].tag
                std::string tag;
                if (auto it_tag = parameter.find("tag"); it_tag != parameter.end()) {
                    if (!it_tag->second.isString()) {
                        return srt::Error(srt::Error::InvalidFormat,
                                          "parameters[] tag must be a string");
                    }
                    tag = stdc::to_lower(it_tag->second.toString());
                } else {
                    return srt::Error(srt::Error::InvalidFormat,
                                      "parameters[] tag must exist and be a string");
                } // parameters[].tag
                auto parameterInfo = [&]() -> Co::InputParameterInfo {
                    if (tag == Co::Tags::Pitch.name()) {
                        return Co::InputParameterInfo{Co::Tags::Pitch};
                    } else if (tag == Co::Tags::Breathiness.name()) {
                        return Co::InputParameterInfo{Co::Tags::Breathiness};
                    } else if (tag == Co::Tags::Energy.name()) {
                        return Co::InputParameterInfo{Co::Tags::Energy};
                    } else if (tag == Co::Tags::Gender.name()) {
                        return Co::InputParameterInfo{Co::Tags::Gender};
                    } else if (tag == Co::Tags::Tension.name()) {
                        return Co::InputParameterInfo{Co::Tags::Tension};
                    } else if (tag == Co::Tags::Velocity.name()) {
                        return Co::InputParameterInfo{Co::Tags::Velocity};
                    } else if (tag == Co::Tags::Voicing.name()) {
                        return Co::InputParameterInfo{Co::Tags::Voicing};
                    } else if (tag == Co::Tags::MouthOpening.name()) {
                        return Co::InputParameterInfo{Co::Tags::MouthOpening};
                    } else if (tag == Co::Tags::ToneShift.name()) {
                        return Co::InputParameterInfo{Co::Tags::ToneShift};
                    }
                    return Co::InputParameterInfo{};
                }();
                if (parameterInfo.tag.name().empty()) {
                    return srt::Error(srt::Error::InvalidFormat,
                                      "parameters[].tag string unknown: " + tag);
                }
                // <END> parsing parameters[].tag

                // parameters[].dynamic optional field, default to false
                if (auto exp = parseValueCurve(parameter, "parameters", parameterInfo.interval,
                                               parameterInfo.values);
                    !exp) {
                    return exp.takeError();
                }

                if (auto it_retake = parameter.find("retake"); it_retake != parameter.end()) {
                    if (!it_retake->second.isObject()) {
                        return srt::Error(srt::Error::InvalidFormat,
                                          "parameters[].retake must be an object");
                    }
                    const auto &retake = it_retake->second.toObject();
                    auto it_retakeStart = retake.find("start");
                    auto it_retakeEnd = retake.find("end");

                    if (!(it_retakeStart != retake.end() && it_retakeStart->second.isNumber()) ||
                        !(it_retakeEnd != retake.end() && it_retakeEnd->second.isNumber())) {
                        return srt::Error(
                            srt::Error::InvalidFormat,
                            R"(parameters[].retake object must contain "start" and "end" keys of numbers)");
                    }
                    parameterInfo.retake = Co::InputParameterInfo::RetakeRange{
                        it_retakeStart->second.toDouble(), it_retakeEnd->second.toDouble()};
                } // parameters[].retake

                outParameters.push_back(std::move(parameterInfo));
            } // parameters[] <ITER>
        } // parameters
        return srt::Expected<void>();
    }

    srt::Expected<void> parseSpeakers(const srt::JsonObject &obj,
                                      std::vector<Co::InputSpeakerInfo> &outSpeakers) {
        if (auto it_speakers = obj.find("speakers"); it_speakers != obj.end()) {
            if (!it_speakers->second.isArray()) {
                return srt::Error(srt::Error::InvalidFormat, "speakers must be an array");
            }
            const auto &speakers = it_speakers->second.toArray();
            outSpeakers.reserve(speakers.size());
            for (const auto &maybeSpeaker : speakers) {
                if (!maybeSpeaker.isObject()) {
                    return srt::Error(srt::Error::InvalidFormat,
                                      "speakers[] array item must be an object");
                }
                const auto &speaker = maybeSpeaker.toObject();
                Co::InputSpeakerInfo speakerInfo;
                if (auto it_name = speaker.find("name"); it_name != speaker.end()) {
                    if (!it_name->second.isString()) {
                        return srt::Error(srt::Error::InvalidFormat,
                                          "speakers[].name must be a string");
                    }
                    speakerInfo.name = it_name->second.toString();
                } else {
                    return srt::Error(srt::Error::InvalidFormat,
                                      "speakers[].name must exist and be a string");
                }
                if (auto exp = parseValueCurve(speaker, "parameters", speakerInfo.interval,
                                               speakerInfo.proportions);
                    !exp) {
                    return exp.takeError();
                }
                outSpeakers.push_back(std::move(speakerInfo));
            }
        } // speakers
        return srt::Expected<void>();
    }

}