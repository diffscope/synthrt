#include "VocoderInterpreter.h"

#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>
#include <dsinfer/Api/Inferences/Vocoder/1/VocoderApiL1.h>
#include <stdcorelib/str.h>

#include <inferutil/ErrorCollector.h>
#include <inferutil/Parser.h>

#include "VocoderInference.h"

namespace ds {

    namespace Co = Api::Common::L1;
    namespace Vo = Api::Vocoder::L1;

    VocoderInterpreter::VocoderInterpreter() = default;

    VocoderInterpreter::~VocoderInterpreter() = default;


    srt::Expected<std::unique_ptr<srt::ContribExports>>
        VocoderInterpreter::createExports(const srt::ContribSpec &spec) const {
        return std::make_unique<Vo::VocoderSchema>();
    }

    srt::Expected<std::unique_ptr<srt::ContribConfiguration>>
        VocoderInterpreter::createConfiguration(const srt::ContribSpec &spec) const {
        const auto &config = spec.manifestConfiguration();
        auto result = std::make_unique<Vo::VocoderConfiguration>();

        // Collect all the errors and return to user
        inferutil::ErrorCollector ec;

        inferutil::ConfigurationParser parser(spec.as<srt::InferenceSpec>(), &ec);

        // [REQUIRED] model, path (json value is string)
        {
            static_assert(std::is_same_v<decltype(result->model), std::filesystem::path>);
            parser.parse_path_required(result->model, "model");
        } // model

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

        // pitchControllable, bool
        {
            static_assert(std::is_same_v<decltype(result->pitchControllable), bool>);
            parser.parse_bool_optional(result->pitchControllable, "pitchControllable");
        }

        if (ec.hasErrors()) {
            return srt::Error{
                srt::Error::InvalidFormat,
                ec.getErrorMessage("error parsing vocoder configuration"),
            };
        }
        return std::move(result);
    }

    srt::Expected<std::unique_ptr<srt::ContribImportOptions>>
        VocoderInterpreter::createImportOptions(const srt::ContribSpec &target,
                                                const srt::JsonValue &options) const {
        return std::make_unique<Vo::VocoderImportOptions>();
    }

    srt::Expected<std::unique_ptr<srt::InferenceExecInstance>>
        VocoderInterpreter::createInference(srt::InferenceSpec &spec,
                                            const srt::ContribImportOptions &,
                                            const srt::InferenceRuntimeOptions &) {
        return std::unique_ptr<srt::InferenceExecInstance>(new VocoderInference(spec));
    }

}