// DO NOT include this file directly.
// Include <inferutil/Parser.h> instead.

#ifndef DSINFER_INFERUTIL_PARSER_IMPL_H
#define DSINFER_INFERUTIL_PARSER_IMPL_H

#ifndef DSINFER_INFERUTIL_PARSER_H
#  error "Parser_impl.h should only be included by Parser.h"
#endif

#include <cstddef>
#include <fstream>
#include <utility>

#include <stdcorelib/str.h>
#include <stdcorelib/path.h>
#include <synthrt/Support/JSON.h>

#include <inferutil/SpeakerEmbedding.h>

namespace ds::inferutil {

    namespace detail {

        class ParamTagMappings {
        public:
            // Add new variance parameters here. Remember to update `varianceKeys`
            inline static constexpr std::array<std::pair<std::string_view, ParamTag>, 5>
                varianceMapping = {
                    std::pair{"energy",        Api::Common::L1::Tags::Energy      },
                    std::pair{"breathiness",   Api::Common::L1::Tags::Breathiness },
                    std::pair{"voicing",       Api::Common::L1::Tags::Voicing     },
                    std::pair{"tension",       Api::Common::L1::Tags::Tension     },
                    std::pair{"mouth_opening", Api::Common::L1::Tags::MouthOpening},
            };

            // Add new transition parameters here. Remember to update `transitionKeys`
            inline static constexpr std::array<std::pair<std::string_view, ParamTag>, 3>
                transitionMapping = {
                    std::pair{"gender",   Api::Common::L1::Tags::Gender  },
                    std::pair{"velocity", Api::Common::L1::Tags::Velocity},
            };

            // Better if these xxxKeys can be automatically generated during compile-time.
            // Tried, but this is not easy in C++17.
            inline static constexpr std::string_view varianceKeys = (R"("energy", )"
                                                                     R"("breathiness", )"
                                                                     R"("voicing", )"
                                                                     R"("tension", )"
                                                                     R"("mouth_opening")");
            inline static constexpr std::string_view transitionKeys = (R"("gender", )"
                                                                       R"("velocity")");
        };

        template <typename Container>
        void insertParamHelper(Container &container, const ParamTag &param) {
            if constexpr (std::is_same_v<Container, std::set<ParamTag>>) {
                container.insert(param);
            } else if constexpr (std::is_same_v<Container, std::vector<ParamTag>>) {
                container.push_back(param);
            } else {
                static_assert(std::is_same_v<Container, void>,
                              "insert_param only supports std::set<ParamTag> "
                              "and std::vector<ParamTag>");
            }
        }

        template <typename Container>
        inline bool tryFindAndInsertVarianceParameters(std::string_view key, Container &out) {
            const auto pred = [&](const auto &pair) { return pair.first == key; };

            constexpr auto &vmap = ParamTagMappings::varianceMapping;
            if (const auto it = std::find_if(vmap.begin(), vmap.end(), pred); it != vmap.end()) {
                insertParamHelper(out, it->second);
                return true;
            }

            return false;
        }

        template <typename Container>
        inline bool tryFindAndInsertTransitionParameters(std::string_view key, Container &out) {
            const auto pred = [&](const auto &pair) { return pair.first == key; };

            constexpr auto &tmap = ParamTagMappings::transitionMapping;
            if (const auto it = std::find_if(tmap.begin(), tmap.end(), pred); it != tmap.end()) {
                insertParamHelper(out, it->second);
                return true;
            }

            return false;
        }

        template <typename Container>
        inline bool tryFindAndInsertParameters(std::string_view key, Container &out) {
            return tryFindAndInsertVarianceParameters(key, out) ||
                   tryFindAndInsertTransitionParameters(key, out);
        }

        template <ParameterType PT, typename Container>
        inline void parse_parameters_common(Container &out, const std::string &fieldName,
                                            const srt::JsonObject &obj, ErrorCollector *ec) {
            if (const auto it = obj.find(fieldName); it != obj.end()) {
                if (it->second.isArray()) {
                    const auto &arr = it->second.toArray();
                    // array index counter for logging purpose
                    size_t index = 0;
                    for (const auto &item : std::as_const(arr)) {
                        if (!item.isString()) {
                            if (ec) {
                                ec->collectError(stdc::formatN(
                                    R"(array field "%1" element at index %2 type mismatch: )"
                                    R"(expected string)",
                                    fieldName, index));
                            }
                            ++index;
                            continue;
                        }
                        const auto paramStr = item.toStringView();

                        if constexpr (PT == ParameterType::All) {
                            if (!tryFindAndInsertParameters(paramStr, out)) {
                                if (ec) {
                                    ec->collectError(stdc::formatN(
                                        R"(array field "%1" element at index %2 invalid: )"
                                        R"(expected one of [%3, %4]; got "%5")",
                                        fieldName, index, ParamTagMappings::varianceKeys,
                                        ParamTagMappings::transitionKeys, paramStr));
                                }
                            }
                        } else if constexpr (PT == ParameterType::Variance) {
                            if (!tryFindAndInsertVarianceParameters(paramStr, out)) {
                                if (ec) {
                                    ec->collectError(stdc::formatN(
                                        R"(array field "%1" element at index %2 invalid: )"
                                        R"(expected one of [%3]; got "%4")",
                                        fieldName, index, ParamTagMappings::varianceKeys,
                                        paramStr));
                                }
                            }
                        } else if constexpr (PT == ParameterType::Transition) {
                            if (!tryFindAndInsertTransitionParameters(paramStr, out)) {
                                if (ec) {
                                    ec->collectError(stdc::formatN(
                                        R"(array field "%1" element at index %2 invalid: )"
                                        R"(expected one of [%3]; got "%4")",
                                        fieldName, index, ParamTagMappings::transitionKeys,
                                        paramStr));
                                }
                            }
                        } else {
                            static_assert(PT == ParameterType::All ||
                                          PT == ParameterType::Variance ||
                                          PT == ParameterType::Transition);
                        }

                        ++index;
                    }
                } else {
                    // not an array (error)
                    if (ec) {
                        ec->collectError("array field \"" + fieldName + "\" type mismatch");
                    }
                }
            } else {
                // nothing to do: optional field
            }
        }
    }

    inline void ConfigurationParser::parse_bool_optional(bool &out, const std::string &fieldName) {
        const auto &config = *pConfig;

        if (const auto it = config.find(fieldName); it != config.end()) {
            if (it->second.isBool()) {
                out = it->second.toBool();
            } else {
                collectError("boolean field \"" + fieldName + "\" type mismatch");
            }
        } else {
            // Nothing to do
        }
    }
    inline void ConfigurationParser::parse_int_optional(int &out, const std::string &fieldName) {
        const auto &config = *pConfig;

        if (const auto it = config.find(fieldName); it != config.end()) {
            if (it->second.isNumber()) {
                out = static_cast<int>(it->second.toInt());
            } else {
                collectError("integer field \"" + fieldName + "\" type mismatch");
            }
        } else {
            // Nothing to do
        }
    }

    inline void ConfigurationParser::parse_positive_int_optional(int &out,
                                                                 const std::string &fieldName) {
        const auto &config = *pConfig;

        if (const auto it = config.find(fieldName); it != config.end()) {
            if (it->second.isNumber()) {
                if (const auto val = static_cast<int>(it->second.toInt()); val > 0) {
                    out = val;
                } else {
                    collectError("integer field \"" + fieldName + "\" must be positive");
                }
            } else {
                collectError("integer field \"" + fieldName + "\" type mismatch");
            }
        } else {
            // Nothing to do
        }
    }
    inline void ConfigurationParser::parse_double_optional(double &out,
                                                           const std::string &fieldName) {
        const auto &config = *pConfig;

        if (const auto it = config.find(fieldName); it != config.end()) {
            if (it->second.isNumber()) {
                out = it->second.toDouble();
            } else {
                collectError("float field \"" + fieldName + "\" type mismatch");
            }
        } else {
            // Nothing to do
        }
    }

    inline void ConfigurationParser::parse_positive_double_optional(double &out,
                                                                    const std::string &fieldName) {
        const auto &config = *pConfig;

        if (const auto it = config.find(fieldName); it != config.end()) {
            if (it->second.isNumber()) {
                if (const auto val = it->second.toDouble(); val > 0) {
                    out = val;
                } else {
                    collectError("float field \"" + fieldName + "\" must be positive");
                }
            } else {
                collectError("float field \"" + fieldName + "\" type mismatch");
            }
        } else {
            // Nothing to do
        }
    }

    inline void ConfigurationParser::parse_path_required(std::filesystem::path &out,
                                                         const std::string &fieldName) {
        const auto &config = *pConfig;

        if (const auto it = config.find(fieldName); it != config.end()) {
            if (!it->second.isString()) {
                collectError("string field \"" + fieldName + "\" type mismatch");
            } else {
                out = stdc::path::clean_path(spec->path() /
                                             stdc::path::from_utf8(it->second.toStringView()));
            }
        } else {
            collectError("string field \"" + fieldName + "\" is missing");
        }
    }

    inline void ConfigurationParser::parse_phonemes(std::map<std::string, int> &out) {
        const auto &config = *pConfig;

        if (const auto it = config.find("phonemes"); it != config.end()) {
            if (!it->second.isString()) {
                collectError(R"(string field "phonemes" type mismatch)");
            } else {
                auto path = spec->path() / stdc::path::from_utf8(it->second.toStringView());
                loadIdMapping(it->first, path, out);
            }
        } else {
            collectError("string field \"phonemes\" is missing");
        }
    }

    inline void ConfigurationParser::parse_melBase_optional(MelBase &out) {
        const auto &config = *pConfig;

        if (const auto it = config.find("melBase"); it != config.end()) {
            const auto melBase = it->second.toString();
            const auto melBaseLower = stdc::to_lower(melBase);
            if (melBaseLower == "e") {
                out = MelBase::MelBase_E;
            } else if (melBaseLower == "10") {
                out = MelBase::MelBase_10;
            } else {
                collectError(stdc::formatN(
                    R"(enum string field "melBase" invalid: expect "e", "10"; got "%1")", melBase));
            }
        } else {
            // Nothing to do
        }
    }

    inline void ConfigurationParser::parse_melScale_optional(MelScale &out) {
        const auto &config = *pConfig;

        if (const auto it = config.find("melScale"); it != config.end()) {
            const auto melScale = it->second.toString();
            const auto melScaleLower = stdc::to_lower(melScale);
            if (melScaleLower == "slaney") {
                out = MelScale::MelScale_Slaney;
            } else if (melScaleLower == "htk") {
                out = MelScale::MelScale_HTK;
            } else {
                collectError(stdc::format(
                    R"(enum string field "melScale" invalid: expect "slaney", "htk"; got "%1")",
                    melScale));
            }
        } else {
            // Nothing to do
        }
    }

    inline void ConfigurationParser::parse_linguisticMode_optional(LinguisticMode &out) {
        const auto &config = *pConfig;

        if (const auto it = config.find("linguisticMode"); it != config.end()) {
            const auto linguisticMode = it->second.toString();
            const auto linguisticModeLower = stdc::to_lower(linguisticMode);
            if (linguisticModeLower == "word") {
                out = LinguisticMode::LM_Word;
            } else if (linguisticModeLower == "phoneme") {
                out = LinguisticMode::LM_Phoneme;
            } else {
                collectError(stdc::format(R"(enum string field "linguisticMode" invalid: )"
                                          R"(expect "word", "phoneme"; got "%1")",
                                          linguisticMode));
            }
        } else {
            // Nothing to do
        }
    }

    inline void ConfigurationParser::parse_languages(bool useLanguageId,
                                                     std::map<std::string, int> &out) {
        const auto &config = *pConfig;

        if (const auto it = config.find("languages"); it != config.end()) {
            if (!it->second.isString()) {
                collectError(R"(string field "languages" type mismatch)");
            } else {
                auto path = spec->path() / stdc::path::from_utf8(it->second.toStringView());
                loadIdMapping(it->first, path, out);
            }
        } else {
            if (useLanguageId) {
                // Missing required `languages` field.
                collectError(R"(string field "languages" is missing)"
                             R"((required when "useLanguageId" is set to true))");
            } else {
                // Nothing to do:
                // `languages` is an optional field when "useLanguageId" is set to false
            }
        }
    }

    inline void ConfigurationParser::parse_hiddenSize(bool useSpeakerEmbedding, int &out) {
        const auto &config = *pConfig;

        if (const auto it = config.find("hiddenSize"); it != config.end()) {
            if (it->second.isNumber()) {
                auto val = static_cast<int>(it->second.toInt());
                if (val <= 0) {
                    collectError(R"(integer field "hiddenSize" must be a positive integer)");
                }
                out = val;
            } else {
                collectError(R"(integer field "hiddenSize" type mismatch)");
            }
        } else {
            if (useSpeakerEmbedding) {
                // Missing required `hiddenSize` field.
                collectError(R"(integer field "hiddenSize" is missing )"
                             R"((required when "useSpeakerEmbedding" is set to true))");
            } else {
                // Nothing to do:
                // `hiddenSize` is an optional field when "useSpeakerEmbedding" is set to false
            }
        }
    }

    inline void ConfigurationParser::parse_speakers_and_load_emb(
        bool useSpeakerEmbedding, int hiddenSize, std::map<std::string, std::vector<float>> &out) {
        const auto &config = *pConfig;

        if (const auto it = config.find("speakers"); it != config.end()) {
            if (!it->second.isObject()) {
                collectError(R"(object field "speakers" type mismatch)");
            } else {
                const auto &obj = it->second.toObject();
                for (const auto &[key, value] : obj) {
                    if (!value.isString()) {
                        collectError(
                            R"(object field "speakers" values type mismatch: string expected)");
                    } else {
                        // Get speaker embedding vector file (.emb) path
                        auto path = stdc::clean_path(spec->path() /
                                                     stdc::path::from_utf8(value.toStringView()));
                        // Try loading .emb file
                        if (auto exp = loadSpeakerEmbedding(hiddenSize, path); exp) {
                            // Successfully loaded .emb file
                            out[key] = exp.take();
                        } else {
                            // Failed to load .emb file
                            collectError(stdc::formatN(
                                R"(could not load speaker ("%1") embedding vector from %2: %3)",
                                key, stdc::path::to_utf8(path), exp.error().what()));
                        }
                    }
                }
            }
        } else {
            if (useSpeakerEmbedding) {
                // Missing required `speakers` field.
                collectError(R"(array field "speakers" is missing )"
                             R"((required when "useSpeakerEmbedding" is set to true))");
            } else {
                // Nothing to do:
                // `speakers` is an optional field when "useSpeakerEmbedding" is set to false
            }
        }
    }

    inline void ConfigurationParser::parse_frameWidth(double &out) {
        const auto &config = *pConfig;

        if (const auto it = config.find("frameWidth"); it != config.end()) {
            // `frameWidth` found
            if (it->second.isNumber()) {
                out = it->second.toDouble();
            } else {
                collectError(R"(float field "frameWidth" type mismatch)");
            }
        } else {
            // `frameWidth` not found, fall back to `sampleRate` and `hopSize`
            auto it_sampleRate = config.find("sampleRate");
            auto it_hopSize = config.find("hopSize");
            if (it_sampleRate != config.end() && it_hopSize != config.end()) {
                // OK: Fields exist
                if (it_sampleRate->second.isNumber() && it_hopSize->second.isNumber()) {
                    // OK: Is a number
                    auto sampleRate = it_sampleRate->second.toDouble();
                    auto hopSize = it_hopSize->second.toDouble();
                    if (sampleRate > 0 && hopSize > 0) {
                        // OK: Is positive
                        out = 1.0 * hopSize / sampleRate;
                    } else {
                        // Error: Not positive
                        collectError(R"(integer fields "hopSize" and "hopSize" must be positive)");
                    }
                } else {
                    // Error: Not a number
                    collectError(R"(integer fields "hopSize" or "hopSize" type mismatch)");
                }
            } else {
                // Error: `frameWidth` not found, and `sampleRate` `hopSize` also not found
                collectError(R"(must specify either "frameWidth" or ("sampleRate and "hopSize")");
            }
        }
    }

    template <ParameterType PT>
    inline void ConfigurationParser::parse_parameters(std::set<ParamTag> &out,
                                                      const std::string &fieldName) {
        const auto &obj = *pConfig;

        detail::parse_parameters_common<PT, std::set<ParamTag>>(out, fieldName, obj, ec);
    }

    template <ParameterType PT>
    inline void ConfigurationParser::parse_parameters(std::vector<ParamTag> &out,
                                                      const std::string &fieldName) {
        const auto &obj = *pConfig;

        detail::parse_parameters_common<PT, std::vector<ParamTag>>(out, fieldName, obj, ec);
    }

    inline bool ConfigurationParser::loadIdMapping(const std::string &fieldName,
                                                   const std::filesystem::path &path,
                                                   std::map<std::string, int> &out) {
        std::ifstream file(path);
        if (!file.is_open()) {
            collectError(stdc::formatN(R"(error loading "%1": %2 file not found)", fieldName,
                                       stdc::path::to_utf8(path)));
            return false;
        }
        file.seekg(0, std::ios::end);
        auto size = file.tellg();
        std::string buffer(size, '\0');
        file.seekg(0);
        file.read(buffer.data(), size);

        std::string errString;
        auto j = srt::JsonValue::fromJson(buffer, true, &errString);
        if (!errString.empty()) {
            if (ec) {
                ec->collectError(std::move(errString));
            }
            return false;
        }

        if (!j.isObject()) {
            collectError(
                stdc::formatN(R"(error loading "%1": outer JSON is not an object)", fieldName));
            return false;
        }

        const auto &obj = j.toObject();
        bool flag = true;
        for (const auto &[key, value] : obj) {
            if (!value.isInt()) {
                flag = false;
                collectError(stdc::formatN(R"(error loading "%1": value of key "%2" is not int)",
                                           fieldName, key));
            } else {
                out[key] = static_cast<int>(value.toInt());
            }
        }
        return flag;
    }

    inline void SchemaParser::parse_bool_optional(bool &out, const std::string &fieldName) {
        const auto &schema = *pSchema;

        if (const auto it = schema.find(fieldName); it != schema.end()) {
            if (it->second.isBool()) {
                out = it->second.toBool();
            } else {
                collectError("boolean field \"" + fieldName + "\" type mismatch");
            }
        } else {
            // Nothing to do
        }
    }

    template <ParameterType PT>
    inline void SchemaParser::parse_parameters(std::set<ParamTag> &out,
                                               const std::string &fieldName) {
        const auto &schema = *pSchema;

        detail::parse_parameters_common<PT, std::set<ParamTag>>(out, fieldName, schema, ec);
    }

    template <ParameterType PT>
    inline void SchemaParser::parse_parameters(std::vector<ParamTag> &out,
                                               const std::string &fieldName) {
        const auto &schema = *pSchema;

        detail::parse_parameters_common<PT, std::vector<ParamTag>>(out, fieldName, schema, ec);
    }

    inline void SchemaParser::parse_string_array_optional(std::vector<std::string> &out,
                                                          const std::string &fieldName) {
        const auto &schema = *pSchema;

        if (const auto it = schema.find(fieldName); it != schema.end()) {
            if (!it->second.isArray()) {
                collectError("array field \"" + fieldName + "\" type mismatch");
            } else {
                const auto &arr = it->second.toArray();
                out.reserve(arr.size());
                for (const auto &item : arr) {
                    if (!item.isString()) {
                        collectError("array field \"" + fieldName +
                                     "\" values type mismatch: string expected");
                    } else {
                        out.emplace_back(item.toString());
                    }
                }
            }
        } else {
            // nothing to do: optional field
        }
    }

    inline void ImportOptionsParser::parse_speakerMapping(std::map<std::string, std::string> &out) {
        const auto &options = *pOptions;

        if (auto it = options.find("speakerMapping"); it != options.end()) {
            auto val = it->second;
            if (!val.isObject()) {
                collectError(R"(object field "speakerMapping" type mismatch)");
                return;
            }

            const auto &speakerMappingObj = val.toObject();
            for (const auto &[speakerKey, speakerValue] : std::as_const(speakerMappingObj)) {
                if (!speakerValue.isString()) {
                    collectError(
                        R"(object field "speakerMapping" values type mismatch: string expected)");
                    continue;
                }

                out[speakerKey] = speakerValue.toString();
            }
        }
    }

    template <ParameterType PT>
    inline void ImportOptionsParser::parse_parameters(std::set<ParamTag> &out,
                                                      const std::string &fieldName) {
        const auto &options = *pOptions;

        detail::parse_parameters_common<PT, std::set<ParamTag>>(out, fieldName, options, ec);
    }

    template <ParameterType PT>
    inline void ImportOptionsParser::parse_parameters(std::vector<ParamTag> &out,
                                                      const std::string &fieldName) {
        const auto &options = *pOptions;

        detail::parse_parameters_common<PT, std::vector<ParamTag>>(out, fieldName, options, ec);
    }
}

#endif // DSINFER_INFERUTIL_PARSER_IMPL_H