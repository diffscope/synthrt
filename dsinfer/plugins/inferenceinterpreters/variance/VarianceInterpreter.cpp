#include "VarianceInterpreter.h"

#include <utility>

#include <dsinfer/Api/Inferences/Variance/1/VarianceApiL1.h>
#include <stdcorelib/str.h>

#include <inferutil/Parser.h>

#include "VarianceInference.h"

namespace ds {

    namespace Co = Api::Common::L1;
    namespace Var = Api::Variance::L1;

    VarianceInterpreter::VarianceInterpreter() = default;

    VarianceInterpreter::~VarianceInterpreter() = default;

    int VarianceInterpreter::apiLevel() const {
        return 1;
    }

    srt::Expected<srt::NO<srt::InferenceSchema>>
        VarianceInterpreter::createSchema(const srt::InferenceSpec *spec) const {
        if (!spec) {
            // fatal error: null pointer, return immediately
            return srt::Error{
                srt::Error::InvalidArgument,
                "fatal in createSchema: InferenceSpec is nullptr",
            };
        }

        auto result = srt::NO<Var::VarianceSchema>::create();

        // Collect all the errors and return to user
        inferutil::ErrorCollector ec;

        inferutil::SchemaParser parser(spec, &ec);

        // speakers, string[]
        {
            static_assert(std::is_same_v<decltype(result->speakers), std::vector<std::string>>);
            parser.parse_string_array_optional(result->speakers, "speakers");
        } // speakers

        // predictions, ParamTag[] (json value is string[])
        {
            static_assert(std::is_same_v<decltype(result->predictions), std::vector<ParamTag>>);
            constexpr auto paramType = inferutil::ParameterType::Variance;
            parser.parse_parameters<paramType>(result->predictions, "predictions");
            if (result->predictions.empty()) {
                ec.collectError("predictions should not be empty");
            }
        }

        if (ec.hasErrors()) {
            return srt::Error{
                srt::Error::InvalidFormat,
                ec.getErrorMessage("error parsing variance schema"),
            };
        }
        return result;
    }

    srt::Expected<srt::NO<srt::InferenceConfiguration>>
        VarianceInterpreter::createConfiguration(const srt::InferenceSpec *spec) const {
        if (!spec) {
            // fatal error: null pointer, return immediately
            return srt::Error{
                srt::Error::InvalidArgument,
                "fatal in createConfiguration: InferenceSpec is nullptr",
            };
        }

        const auto &config = spec->manifestConfiguration();
        auto result = srt::NO<Var::VarianceConfiguration>::create();

        // Collect all the errors and return to user
        inferutil::ErrorCollector ec;

        inferutil::ConfigurationParser parser(spec, &ec);
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
            static_assert(std::is_same_v<decltype(result->linguisticMode), Co::LinguisticMode>);
            parser.parse_linguisticMode_optional(result->linguisticMode);
        }

        // useContinuousAcceleration, bool
        {
            static_assert(std::is_same_v<decltype(result->useContinuousAcceleration), bool>);
            parser.parse_bool_optional(result->useContinuousAcceleration, "useContinuousAcceleration");
        }

        if (ec.hasErrors()) {
            return srt::Error{
                srt::Error::InvalidFormat,
                ec.getErrorMessage("error parsing variance configuration"),
            };
        }
        return result;
    }

    srt::Expected<srt::NO<srt::InferenceImportOptions>>
        VarianceInterpreter::createImportOptions(const srt::InferenceSpec *spec,
                                                 const srt::JsonValue &options) const {
        if (!options.isObject()) {
            return srt::Error{
                srt::Error::InvalidFormat,
                "error parsing variance import options: import options JSON should be an object",
            };
        }
        const auto &obj = options.toObject();
        auto result = srt::NO<Var::VarianceImportOptions>::create();

        // Collect all the errors and return to user
        inferutil::ErrorCollector ec;

        inferutil::ImportOptionsParser parser(spec, &ec, obj);

        // speakerMapping, { string: string }
        {
            static_assert(std::is_same_v<decltype(result->speakerMapping),
                                         std::map<std::string, std::string>>);
            parser.parse_speakerMapping(result->speakerMapping);
        }

        // predictions, set<ParamTag> (json value is string[])
        {
            static_assert(std::is_same_v<decltype(result->predictions), std::set<ParamTag>>);
            constexpr auto paramType = inferutil::ParameterType::Variance;
            parser.parse_parameters<paramType>(result->predictions, "predictions");
            if (result->predictions.empty()) {
                ec.collectError("predictions should not be empty");
            }
        }

        if (ec.hasErrors()) {
            return srt::Error{
                srt::Error::InvalidFormat,
                ec.getErrorMessage("error parsing variance import options"),
            };
        }
        return result;
    }

    srt::Expected<srt::NO<srt::Inference>> VarianceInterpreter::createInference(
        const srt::InferenceSpec *spec, const srt::NO<srt::InferenceImportOptions> &importOptions,
        const srt::NO<srt::InferenceRuntimeOptions> &runtimeOptions) {
        return srt::NO<VarianceInference>::create(spec);
    }

}