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

    srt::Expected<std::unique_ptr<srt::ContribExports>>
        DurationInterpreter::createExports(const srt::ContribSpec &spec) const {
        auto result = std::make_unique<Dur::DurationSchema>();

        // Collect all the errors and return to user
        inferutil::ErrorCollector ec;

        inferutil::SchemaParser parser(spec.as<srt::InferenceSpec>(), &ec);

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
        return std::unique_ptr<srt::ContribExports>(std::move(result));
    }

    srt::Expected<std::unique_ptr<srt::ContribConfiguration>>
        DurationInterpreter::createConfiguration(const srt::ContribSpec &spec) const {
        auto result = std::make_unique<Dur::DurationConfiguration>();

        // Collect all the errors and return to user
        inferutil::ErrorCollector ec;

        inferutil::ConfigurationParser parser(spec.as<srt::InferenceSpec>(), &ec);
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
        return std::unique_ptr<srt::ContribConfiguration>(std::move(result));
    }

    srt::Expected<std::unique_ptr<srt::ContribImportOptions>>
        DurationInterpreter::createImportOptions(const srt::ContribSpec &target,
                                                 const srt::JsonValue &options) const {
        if (!options.isObject()) {
            return srt::Error{
                srt::Error::InvalidFormat,
                "error parsing duration import options: import options JSON should be an object",
            };
        }
        const auto &obj = options.toObject();
        auto result = std::make_unique<Dur::DurationImportOptions>();

        // Collect all the errors and return to user
        inferutil::ErrorCollector ec;

        inferutil::ImportOptionsParser parser(target.as<srt::InferenceSpec>(), &ec, obj);

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
        return std::unique_ptr<srt::ContribImportOptions>(std::move(result));
    }

    srt::Expected<std::unique_ptr<srt::InferenceExecInstance>>
        DurationInterpreter::createInference(srt::InferenceSpec &spec,
                                             const srt::ContribImportOptions &,
                                             const srt::InferenceRuntimeOptions &) {
        return std::unique_ptr<srt::InferenceExecInstance>(new DurationInference(spec));
    }

}
