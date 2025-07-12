#include "DurationInterpreter.h"

#include <fstream>
#include <utility>

#include <dsinfer/Api/Inferences/Duration/1/DurationApiL1.h>
#include <stdcorelib/str.h>

#include <inferutil/Parser.h>

#include "DurationInference.h"

namespace ds {

    namespace Co = Api::Common::L1;
    namespace Dur = Api::Duration::L1;

    DurationInterpreter::DurationInterpreter() = default;

    DurationInterpreter::~DurationInterpreter() = default;

    int DurationInterpreter::apiLevel() const {
        return 1;
    }

    srt::Expected<srt::NO<srt::InferenceSchema>>
        DurationInterpreter::createSchema(const srt::InferenceSpec *spec) const {
        if (!spec) {
            // fatal error: null pointer, return immediately
            return srt::Error{
                srt::Error::InvalidArgument,
                "fatal in createSchema: InferenceSpec is nullptr",
            };
        }

        auto result = srt::NO<Dur::DurationSchema>::create();

        // Collect all the errors and return to user
        inferutil::ErrorCollector ec;

        inferutil::SchemaParser parser(spec, &ec);

        // speakers, string[]
        {
            static_assert(std::is_same_v<decltype(result->speakers), std::vector<std::string>>);
            parser.parse_string_array_optional(result->speakers, "speakers");
        } // speakers

        if (ec.hasErrors()) {
            return srt::Error{
                srt::Error::InvalidFormat,
                ec.getErrorMessage("error parsing duration schema"),
            };
        }
        return result;
    }

    srt::Expected<srt::NO<srt::InferenceConfiguration>>
        DurationInterpreter::createConfiguration(const srt::InferenceSpec *spec) const {
        if (!spec) {
            // fatal error: null pointer, return immediately
            return srt::Error{
                srt::Error::InvalidArgument,
                "fatal in createConfiguration: InferenceSpec is nullptr",
            };
        }

        const auto &config = spec->manifestConfiguration();
        auto result = srt::NO<Dur::DurationConfiguration>::create();

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

        if (ec.hasErrors()) {
            return srt::Error{
                srt::Error::InvalidFormat,
                ec.getErrorMessage("error parsing duration configuration"),
            };
        }
        return result;
    }

    srt::Expected<srt::NO<srt::InferenceImportOptions>>
        DurationInterpreter::createImportOptions(const srt::InferenceSpec *spec,
                                                 const srt::JsonValue &options) const {
        if (!options.isObject()) {
            return srt::Error{
                srt::Error::InvalidFormat,
                "error parsing duration import options: import options JSON should be an object",
            };
        }
        const auto &obj = options.toObject();
        auto result = srt::NO<Dur::DurationImportOptions>::create();

        // Collect all the errors and return to user
        inferutil::ErrorCollector ec;

        inferutil::ImportOptionsParser parser(spec, &ec, obj);

        // speakerMapping
        {
            static_assert(std::is_same_v<decltype(result->speakerMapping),
                                         std::map<std::string, std::string>>);
            parser.parse_speakerMapping(result->speakerMapping);
        } // speakerMapping

        if (ec.hasErrors()) {
            return srt::Error{
                srt::Error::InvalidFormat,
                ec.getErrorMessage("error parsing duration import options"),
            };
        }
        return result;
    }

    srt::Expected<srt::NO<srt::Inference>> DurationInterpreter::createInference(
        const srt::InferenceSpec *spec, const srt::NO<srt::InferenceImportOptions> &importOptions,
        const srt::NO<srt::InferenceRuntimeOptions> &runtimeOptions) {
        return srt::NO<DurationInference>::create(spec);
    }

}