#include "VocoderInterpreter.h"

#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>
#include <dsinfer/Api/Inferences/Vocoder/1/VocoderApiL1.h>
#include <stdcorelib/str.h>

#include <inferutil/ErrorCollector.h>
#include <inferutil/Parser.h>

#include "VocoderInference.h"

namespace srt::svs {

    namespace Co = Api::Common::L1;
    namespace Vo = Api::Vocoder::L1;

    VocoderInterpreter::VocoderInterpreter() = default;

    VocoderInterpreter::~VocoderInterpreter() = default;

    int VocoderInterpreter::apiLevel() const {
        return 1;
    }

    srt::core::Expected<srt::core::NO<srt::svs::InferenceSchema>>
        VocoderInterpreter::createSchema(const srt::svs::InferenceSpec *spec) const {
        return srt::core::NO<Vo::VocoderSchema>::create();
    }

    srt::core::Expected<srt::core::NO<srt::svs::InferenceConfiguration>>
        VocoderInterpreter::createConfiguration(const srt::svs::InferenceSpec *spec) const {
        if (!spec) {
            // fatal error: null pointer, return immediately
            return srt::core::Error{
                srt::core::Error::InvalidArgument,
                "fatal in createConfiguration: InferenceSpec is nullptr",
            };
        }

        const auto &config = spec->manifestConfiguration();
        auto result = srt::core::NO<Vo::VocoderConfiguration>::create();

        // Collect all the errors and return to user
        ds::infer::inferutil::ErrorCollector ec;

        ds::infer::inferutil::ConfigurationParser parser(spec, &ec);

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
            return srt::core::Error{
                srt::core::Error::InvalidFormat,
                ec.getErrorMessage("error parsing vocoder configuration"),
            };
        }
        return result;
    }

    srt::core::Expected<srt::core::NO<srt::svs::InferenceImportOptions>>
        VocoderInterpreter::createImportOptions(const srt::svs::InferenceSpec *spec,
                                                const srt::core::JsonValue &options) const {
        return srt::core::NO<Vo::VocoderImportOptions>::create();
    }

    srt::core::Expected<srt::core::NO<srt::svs::Inference>> VocoderInterpreter::createInference(
        const srt::svs::InferenceSpec *spec, const srt::core::NO<srt::svs::InferenceImportOptions> &importOptions,
        const srt::core::NO<srt::svs::InferenceRuntimeOptions> &runtimeOptions) {
        return srt::core::NO<VocoderInference>::create(spec);
    }

}