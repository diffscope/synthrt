#pragma once

#include <set>
#include <string>
#include <vector>

#include <synthrt/SVS/InferenceContrib.h>

#include <dsinfer/Core/ParamTag.h>
#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>

#include <inferutil/ErrorCollector.h>

namespace ds::infer::inferutil {

    using ::srt::svs::ParamTag;

    enum class ParameterType {
        Variance,
        Transition,
        All,
    };

    class ConfigurationParser {
    private:
        using MelBase = srt::svs::Api::Common::L1::MelBase;
        using MelScale = srt::svs::Api::Common::L1::MelScale;
        using LinguisticMode = srt::svs::Api::Common::L1::LinguisticMode;

    public:
        ConfigurationParser(const srt::svs::InferenceSpec *spec_, ErrorCollector *ec_)
            : m_spec(spec_), m_ec(ec_) {
            m_pConfig = &m_spec->manifestConfiguration();
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
        inline void parse_linguisticMode_optional(LinguisticMode &out);
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

        template <ParameterType PT>
        inline void parse_parameters(std::vector<ParamTag> &out, const std::string &fieldName);

        template <typename T>
        inline void collectError(T &&msg) {
            if (m_ec) {
                m_ec->collectError(std::forward<T>(msg));
            }
        }

    private:
        bool loadIdMapping(const std::string &fieldName, const std::filesystem::path &path,
                           std::map<std::string, int> &out);

        const srt::svs::InferenceSpec *m_spec;
        ErrorCollector *m_ec;
        const srt::core::JsonObject *m_pConfig;
    };

    class SchemaParser {
    public:
        SchemaParser(const srt::svs::InferenceSpec *spec_, ErrorCollector *ec_) : m_spec(spec_), m_ec(ec_) {
            m_pSchema = &m_spec->manifestSchema();
        }

        inline void parse_bool_optional(bool &out, const std::string &fieldName);
        inline void parse_string_array_optional(std::vector<std::string> &out,
                                                const std::string &fieldName);

        template <ParameterType PT>
        inline void parse_parameters(std::set<ParamTag> &out, const std::string &fieldName);

        template <ParameterType PT>
        inline void parse_parameters(std::vector<ParamTag> &out, const std::string &fieldName);

        template <typename T>
        inline void collectError(T &&msg) {
            if (m_ec) {
                m_ec->collectError(std::forward<T>(msg));
            }
        }

    private:
        bool loadIdMapping(const std::string &fieldName, const std::filesystem::path &path,
                           std::map<std::string, int> &out);

        const srt::svs::InferenceSpec *m_spec;
        ErrorCollector *m_ec;
        const srt::core::JsonObject *m_pSchema;
    };

    class ImportOptionsParser {
    public:
        ImportOptionsParser(const srt::svs::InferenceSpec *spec_, ErrorCollector *ec_,
                            const srt::core::JsonObject &options_)
            : m_spec(spec_), m_ec(ec_), m_pOptions(&options_) {
        }

        inline void parse_speakerMapping(std::map<std::string, std::string> &out);

        template <ParameterType PT>
        inline void parse_parameters(std::set<ParamTag> &out, const std::string &fieldName);

        template <ParameterType PT>
        inline void parse_parameters(std::vector<ParamTag> &out, const std::string &fieldName);

        template <typename T>
        inline void collectError(T &&msg) {
            if (m_ec) {
                m_ec->collectError(std::forward<T>(msg));
            }
        }
    private:
        const srt::svs::InferenceSpec *m_spec;
        ErrorCollector *m_ec;
        const srt::core::JsonObject *m_pOptions;
    };
}

// Define the guard expected by detail/Parser_impl.h so it can be safely
// included here. Parser_impl.h is a template-implementation header that must
// only be included by this file; the guard prevents direct inclusion from
// other translation units.
#define DSINFER_INFERUTIL_PARSER_H
#include "detail/Parser_impl.h"
