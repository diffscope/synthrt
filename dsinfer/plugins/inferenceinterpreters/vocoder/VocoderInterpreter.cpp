#include "VocoderInterpreter.h"

#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>
#include <dsinfer/Api/Inferences/Vocoder/1/VocoderApiL1.h>
#include <stdcorelib/str.h>
#include <stdcorelib/path.h>

#include <InterpreterCommon/ErrorCollector.h>

#include "VocoderInference.h"

namespace ds {

    namespace Co = Api::Common::L1;
    namespace Vo = Api::Vocoder::L1;

    static inline std::string formatErrorMessage(const std::string &msgPrefix,
                                                 const std::vector<std::string> &errorList);

    VocoderInterpreter::VocoderInterpreter() = default;

    VocoderInterpreter::~VocoderInterpreter() = default;

    int VocoderInterpreter::apiLevel() const {
        return 1;
    }

    srt::Expected<srt::NO<srt::InferenceSchema>>
        VocoderInterpreter::createSchema(const srt::InferenceSpec *spec) const {
        // TODO: 读取 spec->manifestSchema() 并返回对应的 InferenceSchema 对象
        //       spec->manifestConfiguration() 也是可以读取作为参考的
        return srt::NO<Vo::VocoderSchema>::create();
    }

    srt::Expected<srt::NO<srt::InferenceConfiguration>>
        VocoderInterpreter::createConfiguration(const srt::InferenceSpec *spec) const {
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
        auto result = srt::NO<Vo::VocoderConfiguration>::create();

        // Collect all the errors and return to user
        InterpreterCommon::ErrorCollector ec;

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
                ec.getErrorMessage("error parsing vocoder configuration"),
            };
        }
        return result;
    }

    srt::Expected<srt::NO<srt::InferenceImportOptions>>
        VocoderInterpreter::createImportOptions(const srt::InferenceSpec *spec,
                                                const srt::JsonValue &options) const {
        // TODO: 读取 options 并返回对应的 InferenceImportOptions 对象
        //       spec 中所有内容均可读取作为参考
        return srt::NO<Vo::VocoderImportOptions>::create();
    }

    srt::Expected<srt::NO<srt::Inference>> VocoderInterpreter::createInference(
        const srt::InferenceSpec *spec, const srt::NO<srt::InferenceImportOptions> &importOptions,
        const srt::NO<srt::InferenceRuntimeOptions> &runtimeOptions) {
        // TODO: importOptions 和 runtimeOptions 均可读取作为参考
        return srt::NO<VocoderInference>::create(spec);
    }

}