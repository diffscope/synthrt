#include "AcousticInterpreter.h"

#include <fstream>
#include <utility>

#include <dsinfer/Api/Inferences/Acoustic/1/AcousticApiL1.h>
#include <stdcorelib/str.h>
#include <stdcorelib/path.h>

#include <InterpreterCommon/SpeakerEmbedding.h>
#include <InterpreterCommon/ErrorCollector.h>
#include <InterpreterCommon/IdMappingLoader.h>

#include "AcousticInference.h"

namespace ds {

    namespace Co = Api::Common::L1;
    namespace Ac = Api::Acoustic::L1;

    static constexpr auto varianceTagMapping = std::array{
        std::pair{Co::Tags::Energy.name(),       Co::Tags::Energy      },
        std::pair{Co::Tags::Breathiness.name(),  Co::Tags::Breathiness },
        std::pair{Co::Tags::Voicing.name(),      Co::Tags::Voicing     },
        std::pair{Co::Tags::Tension.name(),      Co::Tags::Tension     },
        std::pair{Co::Tags::MouthOpening.name(), Co::Tags::MouthOpening},
    };

    static constexpr auto transitionTagMapping = std::array{
        std::pair{Co::Tags::Gender.name(),    Co::Tags::Gender   },
        std::pair{Co::Tags::Velocity.name(),  Co::Tags::Velocity },
        std::pair{Co::Tags::ToneShift.name(), Co::Tags::ToneShift},
    };

    template <size_t N>
    static std::string
        getKeysString(const std::array<std::pair<std::string_view, ParamTag>, N> &tagMapping) {
        size_t totalLen = 0;
        for (const auto &[key, _] : tagMapping) {
            totalLen += key.size() + 2; // key + quotes
        }
        if (!tagMapping.empty()) {
            totalLen += (tagMapping.size() - 1) * 2; // ", " between keys
        }

        std::string resultStr;
        resultStr.reserve(totalLen);

        for (size_t i = 0; i < tagMapping.size(); ++i) {
            if (i > 0)
                resultStr += ", ";
            resultStr += '"';
            resultStr += tagMapping[i].first;
            resultStr += '"';
        }
        return resultStr;
    }

    static const std::string &getVarianceParamKeys() {
        // lazy evaluation
        static const std::string keys = [] { return getKeysString(varianceTagMapping); }();
        return keys;
    }

    static const std::string &getTransitionParamKeys() {
        // lazy evaluation
        static const std::string keys = [] { return getKeysString(transitionTagMapping); }();
        return keys;
    }

    static const std::string &getParamKeys() {
        // lazy evaluation
        static const std::string keys = [] {
            return getVarianceParamKeys() + ", " + getTransitionParamKeys();
        }();
        return keys;
    }

    AcousticInterpreter::AcousticInterpreter() = default;

    AcousticInterpreter::~AcousticInterpreter() = default;

    int AcousticInterpreter::apiLevel() const {
        return 1;
    }

    srt::Expected<srt::NO<srt::InferenceSchema>>
        AcousticInterpreter::createSchema(const srt::InferenceSpec *spec) const {
        // TODO: 读取 spec->manifestSchema() 并返回对应的 InferenceSchema 对象
        //       spec->manifestConfiguration() 也是可以读取作为参考的
        if (!spec) {
            // fatal error: null pointer, return immediately
            return srt::Error{
                srt::Error::InvalidArgument,
                "fatal in createSchema: InferenceSpec is nullptr",
            };
        }

        const auto &schema = spec->manifestSchema();
        auto result = srt::NO<Ac::AcousticSchema>::create();

        // Collect all the errors and return to user
        InterpreterCommon::ErrorCollector ec;

        // speakers, string[]
        {
            static_assert(std::is_same_v<decltype(result->speakers), std::vector<std::string>>);
            if (const auto it = schema.find("speakers"); it != schema.end()) {
                if (!it->second.isArray()) {
                    ec.collectError(R"(array field "speakers" type mismatch)");
                } else {
                    const auto &arr = it->second.toArray();
                    for (const auto &item : arr) {
                        if (!item.isString()) {
                            ec.collectError(
                                R"(array field "speakers" values type mismatch: string expected)");
                        } else {
                            result->speakers.emplace_back(item.toString());
                        }
                    }
                }
            } else {
                // nothing to do: `speakers` is an optional field
            }
        } // speakers

        const auto tryFindAndInsert = [&](std::string_view key, const auto &tagMapping,
                                          auto &out) -> bool {
            // out parameter is result->varianceControls or result->transitionControls
            const auto pred = [&](const auto &pair) { return pair.first == key; };

            if (const auto it = std::find_if(tagMapping.begin(), tagMapping.end(), pred);
                it != tagMapping.end()) {
                out.insert(it->second);
                return true;
            }

            return false;
        };

        // varianceControls, set<ParamTag> (json value is string[])
        {
            static_assert(std::is_same_v<decltype(result->varianceControls), std::set<ParamTag>>);

            if (const auto it = schema.find("varianceControls"); it != schema.end()) {
                if (it->second.isArray()) {
                    const auto &arr = it->second.toArray();
                    // array index counter for logging purpose
                    size_t index = 0;
                    for (const auto &item : std::as_const(arr)) {
                        if (!item.isString()) {
                            ec.collectError(stdc::formatN(
                                R"(array field "varianceControls" element at index %1 type mismatch: expected string)",
                                index));
                            ++index;
                            continue;
                        }
                        const auto paramStr = item.toStringView();

                        if (!tryFindAndInsert(paramStr, varianceTagMapping,
                                              result->varianceControls)) {
                            ec.collectError(stdc::formatN(
                                R"(array field "varianceControls" element at index %1 invalid: expected %2; got "%3")",
                                index, getVarianceParamKeys(), paramStr));
                        }

                        ++index;
                    }
                } else {
                    // not an array (error)
                    ec.collectError(R"(array field "varianceControls" type mismatch)");
                }
            } else {
                // nothing to do: `varianceControls` is an optional field
            }
        } // varianceControls

        // transitionControls, set<ParamTag> (json value is string[])
        {
            static_assert(std::is_same_v<decltype(result->transitionControls), std::set<ParamTag>>);

            if (const auto it = schema.find("transitionControls"); it != schema.end()) {
                if (it->second.isArray()) {
                    const auto &arr = it->second.toArray();
                    // array index counter for logging purpose
                    size_t index = 0;
                    for (const auto &item : std::as_const(arr)) {
                        if (!item.isString()) {
                            ec.collectError(stdc::formatN(
                                R"(array field "transitionControls" element at index %1 type mismatch: expected string)",
                                index));
                            ++index;
                            continue;
                        }
                        const auto paramStr = item.toStringView();

                        if (!tryFindAndInsert(paramStr, transitionTagMapping,
                                              result->transitionControls)) {
                            ec.collectError(stdc::formatN(
                                R"(array field "transitionControls" element at index %1 invalid: expected %2; got "%3")",
                                index, getTransitionParamKeys(), paramStr));
                        }

                        ++index;
                    }
                } else {
                    // not an array (error)
                    ec.collectError(R"(array field "transitionControls" type mismatch)");
                }
            } else {
                // nothing to do: `transitionControls` is an optional field
            }
        } // transitionControls

        if (ec.hasErrors()) {
            return srt::Error{
                srt::Error::InvalidFormat,
                ec.getErrorMessage("error parsing acoustic schema"),
            };
        }
        return result;
    }

    srt::Expected<srt::NO<srt::InferenceConfiguration>>
        AcousticInterpreter::createConfiguration(const srt::InferenceSpec *spec) const {
        // TODO: 读取 spec->manifestConfiguration() 并返回对应的 InferenceConfiguration 对象
        //       spec->manifestSchema() 也是可以读取作为参考的
        if (!spec) {
            // fatal error: null pointer, return immediately
            return srt::Error{
                srt::Error::InvalidArgument,
                "fatal in createConfiguration: InferenceSpec is nullptr",
            };
        }

        const auto &config = spec->manifestConfiguration();
        auto result = srt::NO<Ac::AcousticConfiguration>::create();

        // Collect all the errors and return to user
        InterpreterCommon::ErrorCollector ec;

        // phonemes, load file (json value is string of file path)
        {
            static_assert(std::is_same_v<decltype(result->phonemes), std::map<std::string, int>>);
            if (const auto it = config.find("phonemes"); it != config.end()) {
                if (!it->second.isString()) {
                    ec.collectError(R"(string field "phonemes" type mismatch)");
                } else {
                    auto path = spec->path() / stdc::path::from_utf8(it->second.toStringView());
                    InterpreterCommon::loadIdMapping(it->first, path, result->phonemes, &ec);
                }
            } else {
                ec.collectError("string field phonemes is missing");
            }
        } // phonemes

        // useLanguageId, bool
        {
            static_assert(std::is_same_v<decltype(result->useLanguageId), bool>);
            if (const auto it = config.find("useLanguageId"); it != config.end()) {
                if (it->second.isBool()) {
                    result->useLanguageId = it->second.toBool(result->useLanguageId);
                } else {
                    ec.collectError(R"(boolean field "useLanguageId" type mismatch)");
                }
            }
        } // useLanguageId

        // languages, load file (json value is string of file path)
        // [REQUIRED when `useLanguageId` is true]
        {
            static_assert(std::is_same_v<decltype(result->languages), std::map<std::string, int>>);
            if (const auto it = config.find("languages"); it != config.end()) {
                if (!it->second.isString()) {
                    ec.collectError(R"(string field "languages" type mismatch)");
                } else {
                    auto path = spec->path() / stdc::path::from_utf8(it->second.toStringView());
                    InterpreterCommon::loadIdMapping(it->first, path, result->languages, &ec);
                }
            } else {
                if (result->useLanguageId) {
                    // Missing required `languages` field.
                    ec.collectError(R"(string field "languages" is missing)"
                                    R"((required when "useLanguageId" is set to true))");
                } else {
                    // Nothing to do:
                    // `languages` is an optional field when "useLanguageId" is set to false
                }
            }
        } // languages

        // useSpeakerEmbedding, bool
        {
            static_assert(std::is_same_v<decltype(result->useSpeakerEmbedding), bool>);
            if (const auto it = config.find("useSpeakerEmbedding"); it != config.end()) {
                if (it->second.isBool()) {
                    result->useSpeakerEmbedding = it->second.toBool(result->useSpeakerEmbedding);
                } else {
                    ec.collectError(R"(boolean field "useSpeakerEmbedding" type mismatch)");
                }
            }
        } // useSpeakerEmbedding

        // hiddenSize, int
        // [REQUIRED when `useSpeakerEmbedding` is true]
        {
            static_assert(std::is_same_v<decltype(result->hiddenSize), int>);
            if (const auto it = config.find("hiddenSize"); it != config.end()) {
                if (it->second.isNumber()) {
                    result->hiddenSize = static_cast<int>(it->second.toInt(result->hiddenSize));
                    if (result->hiddenSize <= 0) {
                        ec.collectError(R"(integer field "hiddenSize" must be a positive integer)");
                    }
                } else {
                    ec.collectError(R"(integer field "hiddenSize" type mismatch)");
                }
            } else {
                if (result->useSpeakerEmbedding) {
                    // Missing required `hiddenSize` field.
                    ec.collectError(R"(integer field "hiddenSize" is missing )"
                                    R"((required when "useSpeakerEmbedding" is set to true))");
                } else {
                    // Nothing to do:
                    // `hiddenSize` is an optional field when "useSpeakerEmbedding" is set to false
                }
            }
        } // hiddenSize

        // speakers, { string: array } (json value is { string: string } )
        // [REQUIRED when `useSpeakerEmbedding` is true]
        {
            static_assert(std::is_same_v<decltype(result->speakers),
                                         std::map<std::string, std::vector<float>>>);
            if (const auto it = config.find("speakers"); it != config.end()) {
                if (!it->second.isObject()) {
                    ec.collectError(R"(object field "speakers" type mismatch)");
                } else {
                    const auto &obj = it->second.toObject();
                    for (const auto &[key, value] : obj) {
                        if (!value.isString()) {
                            ec.collectError(
                                R"(object field "speakers" values type mismatch: string expected)");
                        } else {
                            // Get speaker embedding vector file (.emb) path
                            auto path = stdc::clean_path(
                                spec->path() / stdc::path::from_utf8(value.toStringView()));
                            // Try loading .emb file
                            if (auto exp = InterpreterCommon::loadSpeakerEmbedding(
                                    result->hiddenSize, path);
                                exp) {
                                // Successfully loaded .emb file
                                result->speakers[key] = exp.take();
                            } else {
                                // Failed to load .emb file
                                ec.collectError(stdc::formatN(
                                    R"(could not load speaker ("%1") embedding vector from %2: %3)",
                                    key, stdc::path::to_utf8(path), exp.error().what()));
                            }
                        }
                    }
                }
            } else {
                if (result->useSpeakerEmbedding) {
                    // Missing required `speakers` field.
                    ec.collectError(R"(array field "speakers" is missing )"
                                    R"((required when "useSpeakerEmbedding" is set to true))");
                } else {
                    // Nothing to do:
                    // `speakers` is an optional field when "useSpeakerEmbedding" is set to false
                }
            }
        } // speakers

        // [REQUIRED] model, path (json value is string)
        {
            static_assert(std::is_same_v<decltype(result->model), std::filesystem::path>);
            if (const auto it = config.find("model"); it != config.end()) {
                if (!it->second.isString()) {
                    ec.collectError(R"(string field "model" type mismatch)");
                } else {
                    result->model = stdc::path::clean_path(
                        spec->path() / stdc::path::from_utf8(it->second.toStringView()));
                }
            } else {
                ec.collectError(R"(string field "model" is missing)");
            }
        } // model

        // parameters, set<ParamTag> (json value is string[])
        {
            static_assert(std::is_same_v<decltype(result->parameters), std::set<ParamTag>>);

            const auto tryFindAndInsertParameters = [&](std::string_view key) -> bool {
                const auto pred = [&](const auto &pair) { return pair.first == key; };

                if (const auto it =
                        std::find_if(varianceTagMapping.begin(), varianceTagMapping.end(), pred);
                    it != varianceTagMapping.end()) {
                    result->parameters.insert(it->second);
                    return true;
                }

                if (const auto it = std::find_if(transitionTagMapping.begin(),
                                                 transitionTagMapping.end(), pred);
                    it != transitionTagMapping.end()) {
                    result->parameters.insert(it->second);
                    return true;
                }

                return false;
            };

            if (const auto it = config.find("parameters"); it != config.end()) {
                if (it->second.isArray()) {
                    const auto &arr = it->second.toArray();
                    // array index counter for logging purpose
                    size_t index = 0;
                    for (const auto &item : std::as_const(arr)) {
                        if (!item.isString()) {
                            ec.collectError(stdc::formatN(
                                R"(array field "parameter" element at index %1 type mismatch: expected string)",
                                index));
                            ++index;
                            continue;
                        }
                        const auto paramStr = item.toStringView();

                        if (!tryFindAndInsertParameters(paramStr)) {
                            ec.collectError(stdc::formatN(
                                R"(array field "parameter" element at index %1 invalid: expected %2; got "%3")",
                                index, getParamKeys(), paramStr));
                        }

                        ++index;
                    }
                } else {
                    // not an array (error)
                    ec.collectError(R"(array field "parameters" type mismatch)");
                }
            } else {
                // nothing to do: `parameters` is an optional field
            }
        } // parameters

        // useContinuousAcceleration, bool
        {
            static_assert(std::is_same_v<decltype(result->useContinuousAcceleration), bool>);
            if (const auto it = config.find("useContinuousAcceleration"); it != config.end()) {
                if (it->second.isBool()) {
                    result->useContinuousAcceleration =
                        it->second.toBool(result->useContinuousAcceleration);
                } else {
                    ec.collectError(R"(boolean field "useContinuousAcceleration" type mismatch)");
                }
            }
        } // useContinuousAcceleration

        // useVariableDepth, bool
        {
            static_assert(std::is_same_v<decltype(result->useVariableDepth), bool>);
            if (const auto it = config.find("useVariableDepth"); it != config.end()) {
                if (it->second.isBool()) {
                    result->useVariableDepth = it->second.toBool(result->useVariableDepth);
                } else {
                    ec.collectError(R"(boolean field "useVariableDepth" type mismatch)");
                }
            }
        } // useVariableDepth

        // maxDepth, double
        {
            static_assert(std::is_same_v<decltype(result->maxDepth), double>);
            if (const auto it = config.find("maxDepth"); it != config.end()) {
                if (it->second.isNumber()) {
                    result->maxDepth = it->second.toDouble(result->maxDepth);
                } else {
                    ec.collectError(R"(float field "maxDepth" type mismatch)");
                }
            }
        } // maxDepth

        // sampleRate, int
        {
            static_assert(std::is_same_v<decltype(result->sampleRate), int>);
            if (const auto it = config.find("sampleRate"); it != config.end()) {
                if (it->second.isNumber()) {
                    result->sampleRate = static_cast<int>(it->second.toInt(result->sampleRate));
                } else {
                    ec.collectError(R"(integer field "sampleRate" type mismatch)");
                }
            }
        } // sampleRate

        // hopSize, int
        {
            static_assert(std::is_same_v<decltype(result->hopSize), int>);
            if (const auto it = config.find("hopSize"); it != config.end()) {
                if (it->second.isNumber()) {
                    result->hopSize = static_cast<int>(it->second.toInt(result->hopSize));
                } else {
                    ec.collectError(R"(integer field "hopSize" type mismatch)");
                }
            }
        } // hopSize

        // winSize, int
        {
            static_assert(std::is_same_v<decltype(result->winSize), int>);
            if (const auto it = config.find("winSize"); it != config.end()) {
                if (it->second.isNumber()) {
                    result->winSize = static_cast<int>(it->second.toInt(result->winSize));
                } else {
                    ec.collectError(R"(integer field "winSize" type mismatch)");
                }
            }
        } // winSize

        // fftSize, int
        {
            static_assert(std::is_same_v<decltype(result->fftSize), int>);
            if (const auto it = config.find("fftSize"); it != config.end()) {
                if (it->second.isNumber()) {
                    result->fftSize = static_cast<int>(it->second.toInt(result->fftSize));
                } else {
                    ec.collectError(R"(integer field "fftSize" type mismatch)");
                }
            }
        } // fftSize

        // melChannels, int
        {
            static_assert(std::is_same_v<decltype(result->melChannels), int>);
            if (const auto it = config.find("melChannels"); it != config.end()) {
                if (it->second.isNumber()) {
                    result->melChannels = static_cast<int>(it->second.toInt(result->melChannels));
                } else {
                    ec.collectError(R"(integer field "melChannels" type mismatch)");
                }
            }
        } // melChannels

        // melMinFreq, int
        {
            static_assert(std::is_same_v<decltype(result->melMinFreq), int>);
            if (const auto it = config.find("melMinFreq"); it != config.end()) {
                if (it->second.isNumber()) {
                    result->melMinFreq = static_cast<int>(it->second.toInt(result->melMinFreq));
                } else {
                    ec.collectError(R"(integer field "melMinFreq" type mismatch)");
                }
            }
        } // melMinFreq

        // melMaxFreq, int
        {
            static_assert(std::is_same_v<decltype(result->melMaxFreq), int>);
            if (const auto it = config.find("melMaxFreq"); it != config.end()) {
                if (it->second.isNumber()) {
                    result->melMaxFreq = static_cast<int>(it->second.toInt(result->melMaxFreq));
                } else {
                    ec.collectError(R"(integer field "melMaxFreq" type mismatch)");
                }
            }
        } // melMaxFreq

        // melBase, enum (json values are strings, case-insensitive)
        {
            static_assert(std::is_same_v<decltype(result->melBase), Co::MelBase>);
            if (const auto it = config.find("melBase"); it != config.end()) {
                const auto melBase = it->second.toString();
                const auto melBaseLower = stdc::to_lower(melBase);
                if (melBaseLower == "e") {
                    result->melBase = Co::MelBase_E;
                } else if (melBaseLower == "10") {
                    result->melBase = Co::MelBase_10;
                } else {
                    ec.collectError(stdc::formatN(
                        R"(enum string field "melBase" invalid: expect "e", "10"; got "%1")",
                        melBase));
                }
            }
        } // melBase

        // melScale, enum (json value is string, case-insensitive)
        {
            static_assert(std::is_same_v<decltype(result->melScale), Co::MelScale>);
            if (const auto it = config.find("melScale"); it != config.end()) {
                const auto melScale = it->second.toString();
                const auto melScaleLower = stdc::to_lower(melScale);
                if (melScaleLower == "slaney") {
                    result->melScale = Co::MelScale_Slaney;
                } else if (melScaleLower == "htk") {
                    result->melScale = Co::MelScale_HTK;
                } else {
                    ec.collectError(stdc::format(
                        R"(enum string field "melScale" invalid: expect "slaney", "htk"; got "%1")",
                        melScale));
                }
            }
        } // melScale

        if (ec.hasErrors()) {
            return srt::Error{
                srt::Error::InvalidFormat,
                ec.getErrorMessage("error parsing acoustic configuration"),
            };
        }
        return result;
    }

    srt::Expected<srt::NO<srt::InferenceImportOptions>>
        AcousticInterpreter::createImportOptions(const srt::InferenceSpec *spec,
                                                 const srt::JsonValue &options) const {
        // TODO: 读取 options 并返回对应的 InferenceImportOptions 对象
        //       spec 中所有内容均可读取作为参考
        if (!options.isObject()) {
            return srt::Error{
                srt::Error::InvalidFormat,
                "invalid acoustic import options format: import options JSON should be an object",
            };
        }
        const auto &obj = options.toObject();
        auto result = srt::NO<Ac::AcousticImportOptions>::create();

        // speakerMapping
        {
            static_assert(std::is_same_v<decltype(result->speakerMapping),
                                         std::map<std::string, std::string>>);
            if (auto it = obj.find("speakerMapping"); it != obj.end()) {
                auto val = it->second;
                if (!val.isObject()) {
                    return srt::Error{
                        srt::Error::InvalidFormat,
                        "invalid acoustic import options format: "
                        R"(object field "speakerMapping" type mismatch)",
                    };
                }
                const auto &speakerMappingObj = val.toObject();
                for (const auto &[speakerKey, speakerValue] : std::as_const(speakerMappingObj)) {
                    if (!speakerValue.isString()) {
                        return srt::Error{
                            srt::Error::InvalidFormat,
                            "invalid acoustic import options format: "
                            R"(object field "speakerMapping" values type mismatch: string expected)",
                        };
                    }
                    result->speakerMapping[speakerKey] = speakerValue.toString();
                }
            }
        } // speakerMapping

        return result;
    }

    srt::Expected<srt::NO<srt::Inference>> AcousticInterpreter::createInference(
        const srt::InferenceSpec *spec, const srt::NO<srt::InferenceImportOptions> &importOptions,
        const srt::NO<srt::InferenceRuntimeOptions> &runtimeOptions) {
        // TODO: importOptions 和 runtimeOptions 均可读取作为参考
        return srt::NO<AcousticInference>::create(spec);
    }

}