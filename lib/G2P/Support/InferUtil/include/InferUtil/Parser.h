#ifndef SRT_G2P_PLUGINS_INFERUTIL_PARSER_H
#define SRT_G2P_PLUGINS_INFERUTIL_PARSER_H

#include <string>
#include <vector>

#include <InferUtil/ErrorCollector.h>
#include <synthrt/Core/Module/Module.h>
#include <synthrt/G2P/Package/Package.h>

#include "Verifier.h"

namespace srt::g2p::plugins::InferUtil
{
    enum class ParameterType {
        Variance,
        Transition,
        All,
    };

    class ConfigurationParser {
    public:
        ConfigurationParser(const srt::g2p::ModuleSpec *spec_, ErrorCollector *ec_) : spec(spec_), ec(ec_) {
            pConfig = &spec->manifestConfiguration();
        }

        inline void parse_bool_optional(bool &out, const std::string &fieldName);
        inline void parse_int_optional(int &out, const std::string &fieldName);
        inline void parse_positive_int_optional(int &out, const std::string &fieldName);
        inline void parse_double_optional(double &out, const std::string &fieldName);
        inline void parse_positive_double_optional(double &out, const std::string &fieldName);
        inline void parse_string_required(std::string &out, const std::string &fieldName);
        inline void parse_path_required(std::filesystem::path &out, const std::string &fieldName);
        inline void parse_phonemes(std::map<std::string, int> &out, const std::string &fieldName);
        inline void parse_verify_required(std::vector<VerifyEntry> &out, const std::string &fieldName);
        inline void parse_stringVec_required(std::vector<std::string> &out, const std::string &fieldName);

        template <typename T>
        void collectError(T &&msg) {
            if (ec) {
                ec->collectError(std::forward<T>(msg));
            }
        }

    private:
        bool loadIdMapping(const std::string &fieldName, const std::filesystem::path &path,
                           std::map<std::string, int> &out);

        const srt::g2p::ModuleSpec *spec;
        ErrorCollector *ec;
        const srt::core::JsonObject *pConfig;
    };
} // namespace srt::g2p::plugins::InferUtil

#include "detail/Parser_impl.h"

#endif // SRT_G2P_PLUGINS_INFERUTIL_PARSER_H
