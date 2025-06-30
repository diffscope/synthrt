#ifndef DSINFER_INTERPRETER_COMMON_PARSER_H
#define DSINFER_INTERPRETER_COMMON_PARSER_H

#include <set>
#include <string>
#include <vector>

#include <synthrt/SVS/InferenceContrib.h>

#include <dsinfer/Core/ParamTag.h>
#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>

#include <InterpreterCommon/ErrorCollector.h>

namespace ds::InterpreterCommon {

    enum class ParameterType {
        Variance,
        Transition,
        All,
    };

    class ConfigurationParser {
    private:
        using MelBase = Api::Common::L1::MelBase;
        using MelScale = Api::Common::L1::MelScale;

    public:
        ConfigurationParser(const srt::InferenceSpec *spec_, ErrorCollector *ec_)
            : spec(spec_), ec(ec_) {
            pConfig = &spec->manifestConfiguration();
        }

        inline void parse_bool_optional(bool &out, const std::string &fieldName);
        inline void parse_int_optional(int &out, const std::string &fieldName);
        inline void parse_positive_int_optional(int &out, const std::string &fieldName);
        inline void parse_double_optional(double &out, const std::string &fieldName);
        inline void parse_positive_double_optional(double &out, const std::string &fieldName);
        inline void parse_path_required(std::filesystem::path &out, const std::string &fieldName);

        inline void parse_phonemes(std::map<std::string, int> &out);
        inline void parse_melBase_optional(MelBase &out);
        inline void parse_melScale_optional(MelScale &out);
        inline void parse_languages(bool useLanguageId, std::map<std::string, int> &out);
        inline void parse_hiddenSize(bool useSpeakerEmbedding, int &out);
        inline void parse_speakers_and_load_emb(bool useSpeakerEmbedding, int hiddenSize,
                                                std::map<std::string, std::vector<float>> &out);

        /// First, try parsing `frameWidth`.
        ///
        /// If not found, try parsing `sampleRate` and `hopSize`,
        /// calculate frameWidth = hopSize / sampleRate
        ///
        /// If all those parameters not found, collect an error.
        inline void parse_frameWidth(double &out);

        template <ParameterType PT>
        inline void parse_parameters(std::set<ParamTag> &out, const std::string &fieldName);

        template <typename T>
        inline void collectError(T &&msg) {
            if (ec) {
                ec->collectError(std::forward<T>(msg));
            }
        }

    private:
        bool loadIdMapping(const std::string &fieldName, const std::filesystem::path &path,
                           std::map<std::string, int> &out);

        const srt::InferenceSpec *spec;
        ErrorCollector *ec;
        const srt::JsonObject *pConfig;
    };

    class SchemaParser {
    public:
        SchemaParser(const srt::InferenceSpec *spec_, ErrorCollector *ec_) : spec(spec_), ec(ec_) {
            pSchema = &spec->manifestSchema();
        }

        inline void parse_bool_optional(bool &out, const std::string &fieldName);
        inline void parse_string_array_optional(std::vector<std::string> &out,
                                                const std::string &fieldName);

        template <ParameterType PT>
        inline void parse_parameters(std::set<ParamTag> &out, const std::string &fieldName);

        template <typename T>
        inline void collectError(T &&msg) {
            if (ec) {
                ec->collectError(std::forward<T>(msg));
            }
        }

    private:
        bool loadIdMapping(const std::string &fieldName, const std::filesystem::path &path,
                           std::map<std::string, int> &out);

        const srt::InferenceSpec *spec;
        ErrorCollector *ec;
        const srt::JsonObject *pSchema;
    };

    class ImportOptionsParser {
    public:
        ImportOptionsParser(const srt::InferenceSpec *spec_, ErrorCollector *ec_,
                            const srt::JsonObject &options_)
            : spec(spec_), ec(ec_), pOptions(&options_) {
        }

        inline void parse_speakerMapping(std::map<std::string, std::string> &out);

        template <typename T>
        inline void collectError(T &&msg) {
            if (ec) {
                ec->collectError(std::forward<T>(msg));
            }
        }
    private:
        const srt::InferenceSpec *spec;
        ErrorCollector *ec;
        const srt::JsonObject *pOptions;
    };
}

#include "detail/Parser_impl.h"

#endif // DSINFER_INTERPRETER_COMMON_PARSER_H