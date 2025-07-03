#include "PitchInterpreter.h"

#include <utility>

#include <dsinfer/Api/Inferences/Pitch/1/PitchApiL1.h>
#include <stdcorelib/str.h>

#include <InterpreterCommon/Parser.h>

#include "PitchInference.h"

namespace ds {

    namespace Co = Api::Common::L1;
    namespace Pit = Api::Pitch::L1;

    PitchInterpreter::PitchInterpreter() = default;

    PitchInterpreter::~PitchInterpreter() = default;

    int PitchInterpreter::apiLevel() const {
        return 1;
    }

    srt::Expected<srt::NO<srt::InferenceSchema>>
        PitchInterpreter::createSchema(const srt::InferenceSpec *spec) const {
        // TODO: 读取 spec->manifestSchema() 并返回对应的 InferenceSchema 对象
        //       spec->manifestConfiguration() 也是可以读取作为参考的
        if (!spec) {
            // fatal error: null pointer, return immediately
            return srt::Error{
                srt::Error::InvalidArgument,
                "fatal in createSchema: InferenceSpec is nullptr",
            };
        }

        auto result = srt::NO<Pit::PitchSchema>::create();

        // Collect all the errors and return to user
        InterpreterCommon::ErrorCollector ec;

        InterpreterCommon::SchemaParser parser(spec, &ec);

        // speakers, string[]
        {
            static_assert(std::is_same_v<decltype(result->speakers), std::vector<std::string>>);
            parser.parse_string_array_optional(result->speakers, "speakers");
        } // speakers

        // allowExpressiveness, bool
        {
            static_assert(std::is_same_v<decltype(result->allowExpressiveness), bool>);
            parser.parse_bool_optional(result->allowExpressiveness, "allowExpressiveness");
        }

        if (ec.hasErrors()) {
            return srt::Error{
                srt::Error::InvalidFormat,
                ec.getErrorMessage("error parsing pitch schema"),
            };
        }
        return result;
    }

    srt::Expected<srt::NO<srt::InferenceConfiguration>>
        PitchInterpreter::createConfiguration(const srt::InferenceSpec *spec) const {
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
        auto result = srt::NO<Pit::PitchConfiguration>::create();

        // Collect all the errors and return to user
        InterpreterCommon::ErrorCollector ec;

        InterpreterCommon::ConfigurationParser parser(spec, &ec);
        // phonemes, load file (json value is string of file path)
        {
            static_assert(std::is_same_v<decltype(result->phonemes), std::map<std::string, int>>);
            parser.parse_phonemes(result->phonemes);
        } // phonemes

        // useLanguageId, bool
        {
            static_assert(std::is_same_v<decltype(result->useLanguageId), bool>);
            parser.parse_bool_optional(result->useLanguageId, "useLanguageId");
        } // useLanguageId

        // languages, load file (json value is string of file path)
        // [REQUIRED when `useLanguageId` is true]
        {
            static_assert(std::is_same_v<decltype(result->languages), std::map<std::string, int>>);
            parser.parse_languages(result->useLanguageId, result->languages);
        } // languages

        // useSpeakerEmbedding, bool
        {
            static_assert(std::is_same_v<decltype(result->useSpeakerEmbedding), bool>);
            parser.parse_bool_optional(result->useSpeakerEmbedding, "useSpeakerEmbedding");
        } // useSpeakerEmbedding

        // hiddenSize, int
        // [REQUIRED when `useSpeakerEmbedding` is true]
        {
            static_assert(std::is_same_v<decltype(result->hiddenSize), int>);
            parser.parse_hiddenSize(result->useSpeakerEmbedding, result->hiddenSize);
        } // hiddenSize

        // speakers, { string: array } (json value is { string: string } )
        // [REQUIRED when `useSpeakerEmbedding` is true]
        {
            static_assert(std::is_same_v<decltype(result->speakers),
                                         std::map<std::string, std::vector<float>>>);
            parser.parse_speakers_and_load_emb(result->useSpeakerEmbedding, result->hiddenSize, result->speakers);
        } // speakers

        // [REQUIRED] encoder, path (json value is string)
        {
            static_assert(std::is_same_v<decltype(result->encoder), std::filesystem::path>);
            parser.parse_path_required(result->encoder, "encoder");
        } // encoder

        // [REQUIRED] predictor, path (json value is string)
        {
            static_assert(std::is_same_v<decltype(result->predictor), std::filesystem::path>);
            parser.parse_path_required(result->predictor, "predictor");
        } // predictor

        // [REQUIRED] frameWidth, double
        // json value can be either:
        //   frameWidth (double)
        // or:
        //   sampleRate (int), hopSize (int) [frameWidth = hopSize / sampleRate]
        {
            static_assert(std::is_same_v<decltype(result->frameWidth), double>);
            parser.parse_frameWidth(result->frameWidth);
        } // frameWidth

        // linguisticMode, enum (json value is string)
        {
            static_assert(std::is_same_v<decltype(result->linguisticMode), Co::LinguistMode>);
            parser.parse_linguisticMode_optional(result->linguisticMode);
        }

        // useExpressiveness, bool
        {
            static_assert(std::is_same_v<decltype(result->useExpressiveness), bool>);
            parser.parse_bool_optional(result->useExpressiveness, "useExpressiveness");
        }

        // useRestFlags, bool
        {
            static_assert(std::is_same_v<decltype(result->useRestFlags), bool>);
            parser.parse_bool_optional(result->useRestFlags, "useRestFlags");
        }

        // useContinuousAcceleration, bool
        {
            static_assert(std::is_same_v<decltype(result->useContinuousAcceleration), bool>);
            parser.parse_bool_optional(result->useContinuousAcceleration, "useContinuousAcceleration");
        }

        if (ec.hasErrors()) {
            return srt::Error{
                srt::Error::InvalidFormat,
                ec.getErrorMessage("error parsing pitch configuration"),
            };
        }
        return result;
    }

    srt::Expected<srt::NO<srt::InferenceImportOptions>>
        PitchInterpreter::createImportOptions(const srt::InferenceSpec *spec,
                                                 const srt::JsonValue &options) const {
        // TODO: 读取 options 并返回对应的 InferenceImportOptions 对象
        //       spec 中所有内容均可读取作为参考
        if (!options.isObject()) {
            return srt::Error{
                srt::Error::InvalidFormat,
                "error parsing pitch import options: import options JSON should be an object",
            };
        }
        const auto &obj = options.toObject();
        auto result = srt::NO<Pit::PitchImportOptions>::create();

        // Collect all the errors and return to user
        InterpreterCommon::ErrorCollector ec;

        InterpreterCommon::ImportOptionsParser parser(spec, &ec, obj);

        // speakerMapping
        {
            static_assert(std::is_same_v<decltype(result->speakerMapping),
                                         std::map<std::string, std::string>>);
            parser.parse_speakerMapping(result->speakerMapping);
        } // speakerMapping

        if (ec.hasErrors()) {
            return srt::Error{
                srt::Error::InvalidFormat,
                ec.getErrorMessage("error parsing pitch import options"),
            };
        }
        return result;
    }

    srt::Expected<srt::NO<srt::Inference>> PitchInterpreter::createInference(
        const srt::InferenceSpec *spec, const srt::NO<srt::InferenceImportOptions> &importOptions,
        const srt::NO<srt::InferenceRuntimeOptions> &runtimeOptions) {
        // TODO: importOptions 和 runtimeOptions 均可读取作为参考
        return srt::NO<PitchInference>::create(spec);
    }

}