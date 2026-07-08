#include "AcousticInterpreter.h"

#include <dsinfer/Api/Inferences/Acoustic/1/AcousticApiL1.h>
#include <stdcorelib/str.h>

#include <inferutil/ErrorCollector.h>
#include <inferutil/Parser.h>

#include "AcousticInference.h"

namespace srt::svs {

    namespace Co = Api::Common::L1;
    namespace Ac = Api::Acoustic::L1;

    AcousticInterpreter::AcousticInterpreter() = default;

    AcousticInterpreter::~AcousticInterpreter() = default;

    int AcousticInterpreter::apiLevel() const {
        return 1;
    }

    srt::core::Expected<srt::core::NO<srt::svs::InferenceSchema>>
        AcousticInterpreter::createSchema(const srt::svs::InferenceSpec *spec) const {
        if (!spec) {
            // fatal error: null pointer, return immediately
            return srt::core::Error{
                srt::core::Error::InvalidArgument,
                "fatal in createSchema: InferenceSpec is nullptr",
            };
        }

        const auto &schema = spec->manifestSchema();
        auto result = srt::core::NO<Ac::AcousticSchema>::create();

        // Collect all the errors and return to user
        ds::infer::inferutil::ErrorCollector ec;

        ds::infer::inferutil::SchemaParser parser(spec, &ec);

        // speakers, string[]
        {
            static_assert(std::is_same_v<decltype(result->speakers), std::vector<std::string>>);
            parser.parse_string_array_optional(result->speakers, "speakers");
        } // speakers

        // varianceControls, set<ParamTag> (json value is string[])
        {
            static_assert(std::is_same_v<decltype(result->varianceControls), std::set<ParamTag>>);
            parser.parse_parameters<ds::infer::inferutil::ParameterType::Variance>(
                result->varianceControls, "varianceControls");
        } // varianceControls

        // transitionControls, set<ParamTag> (json value is string[])
        {
            static_assert(std::is_same_v<decltype(result->transitionControls), std::set<ParamTag>>);
            parser.parse_parameters<ds::infer::inferutil::ParameterType::Transition>(
                result->transitionControls, "transitionControls");
        } // transitionControls

        if (ec.hasErrors()) {
            return srt::core::Error{
                srt::core::Error::InvalidFormat,
                ec.getErrorMessage("error parsing acoustic schema"),
            };
        }
        return result;
    }

    srt::core::Expected<srt::core::NO<srt::svs::InferenceConfiguration>>
        AcousticInterpreter::createConfiguration(const srt::svs::InferenceSpec *spec) const {
        if (!spec) {
            // fatal error: null pointer, return immediately
            return srt::core::Error{
                srt::core::Error::InvalidArgument,
                "fatal in createConfiguration: InferenceSpec is nullptr",
            };
        }

        const auto &config = spec->manifestConfiguration();
        auto result = srt::core::NO<Ac::AcousticConfiguration>::create();

        // Collect all the errors and return to user
        ds::infer::inferutil::ErrorCollector ec;

        ds::infer::inferutil::ConfigurationParser parser(spec, &ec);
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
            parser.parse_speakers_and_load_emb(result->useSpeakerEmbedding, result->hiddenSize,
                                               result->speakers);
        } // speakers

        // [REQUIRED] model, path (json value is string)
        {
            static_assert(std::is_same_v<decltype(result->model), std::filesystem::path>);
            parser.parse_path_required(result->model, "model");
        } // model

        // parameters, set<ParamTag> (json value is string[])
        {
            static_assert(std::is_same_v<decltype(result->parameters), std::set<ParamTag>>);
            parser.parse_parameters<ds::infer::inferutil::ParameterType::All>(result->parameters,
                                                                           "parameters");
        } // parameters

        // useContinuousAcceleration, bool
        {
            static_assert(std::is_same_v<decltype(result->useContinuousAcceleration), bool>);
            parser.parse_bool_optional(result->useContinuousAcceleration,
                                       "useContinuousAcceleration");
        } // useContinuousAcceleration

        // useVariableDepth, bool
        {
            static_assert(std::is_same_v<decltype(result->useVariableDepth), bool>);
            parser.parse_bool_optional(result->useVariableDepth, "useVariableDepth");
        } // useVariableDepth

        // maxDepth, double
        {
            static_assert(std::is_same_v<decltype(result->maxDepth), double>);
            parser.parse_positive_double_optional(result->maxDepth, "maxDepth");
        } // maxDepth

        // sampleRate, int
        {
            static_assert(std::is_same_v<decltype(result->sampleRate), int>);
            parser.parse_positive_int_optional(result->sampleRate, "sampleRate");
        } // sampleRate

        // hopSize, int
        {
            static_assert(std::is_same_v<decltype(result->hopSize), int>);
            parser.parse_positive_int_optional(result->hopSize, "hopSize");
        } // hopSize

        // winSize, int
        {
            static_assert(std::is_same_v<decltype(result->winSize), int>);
            parser.parse_positive_int_optional(result->winSize, "winSize");
        } // winSize

        // fftSize, int
        {
            static_assert(std::is_same_v<decltype(result->fftSize), int>);
            parser.parse_positive_int_optional(result->fftSize, "fftSize");
        } // fftSize

        // melChannels, int
        {
            static_assert(std::is_same_v<decltype(result->melChannels), int>);
            parser.parse_positive_int_optional(result->melChannels, "melChannels");
        } // melChannels

        // melMinFreq, int
        {
            static_assert(std::is_same_v<decltype(result->melMinFreq), int>);
            parser.parse_positive_int_optional(result->melMinFreq, "melMinFreq");
        } // melMinFreq

        // melMaxFreq, int
        {
            static_assert(std::is_same_v<decltype(result->melMaxFreq), int>);
            parser.parse_positive_int_optional(result->melMaxFreq, "melMaxFreq");
        } // melMaxFreq

        // melBase, enum (json values are strings, case-insensitive)
        {
            static_assert(std::is_same_v<decltype(result->melBase), Co::MelBase>);
            parser.parse_melBase_optional(result->melBase);
        } // melBase

        // melScale, enum (json value is string, case-insensitive)
        {
            static_assert(std::is_same_v<decltype(result->melScale), Co::MelScale>);
            parser.parse_melScale_optional(result->melScale);
        } // melScale

        if (ec.hasErrors()) {
            return srt::core::Error{
                srt::core::Error::InvalidFormat,
                ec.getErrorMessage("error parsing acoustic configuration"),
            };
        }
        return result;
    }

    srt::core::Expected<srt::core::NO<srt::svs::InferenceImportOptions>>
        AcousticInterpreter::createImportOptions(const srt::svs::InferenceSpec *spec,
                                                 const srt::core::JsonValue &options) const {
        if (!options.isObject()) {
            return srt::core::Error{
                srt::core::Error::InvalidFormat,
                "error parsing acoustic import options: import options JSON should be an object",
            };
        }
        const auto &obj = options.toObject();
        auto result = srt::core::NO<Ac::AcousticImportOptions>::create();

        // Collect all the errors and return to user
        ds::infer::inferutil::ErrorCollector ec;

        ds::infer::inferutil::ImportOptionsParser parser(spec, &ec, obj);

        // speakerMapping
        {
            static_assert(std::is_same_v<decltype(result->speakerMapping),
                                         std::map<std::string, std::string>>);
            parser.parse_speakerMapping(result->speakerMapping);
        } // speakerMapping

        if (ec.hasErrors()) {
            return srt::core::Error{
                srt::core::Error::InvalidFormat,
                ec.getErrorMessage("error parsing acoustic import options"),
            };
        }
        return result;
    }

    srt::core::Expected<srt::core::NO<srt::svs::Inference>> AcousticInterpreter::createInference(
        const srt::svs::InferenceSpec *spec, const srt::core::NO<srt::svs::InferenceImportOptions> &importOptions,
        const srt::core::NO<srt::svs::InferenceRuntimeOptions> &runtimeOptions) {
        return srt::core::NO<AcousticInference>::create(spec);
    }

}