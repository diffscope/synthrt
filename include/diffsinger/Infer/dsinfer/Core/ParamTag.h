#pragma once

#include <string_view>
#include <type_traits>

namespace srt::svs {

    class ParamTag {
    public:
        inline constexpr ParamTag() = default;

        template <size_t N>
        inline constexpr ParamTag(const char (&name)[N]) : m_name(name, N - 1) {
        }

        inline constexpr std::string_view name() const {
            return m_name;
        }

        inline bool operator==(const ParamTag &RHS) const {
            return m_name == RHS.m_name;
        }

        inline bool operator!=(const ParamTag &RHS) const {
            return m_name != RHS.m_name;
        }

        inline bool operator<(const ParamTag &RHS) const {
            return m_name < RHS.m_name;
        }

        inline bool operator>(const ParamTag &RHS) const {
            return m_name > RHS.m_name;
        }

        inline bool operator<=(const ParamTag &RHS) const {
            return m_name <= RHS.m_name;
        }

        inline bool operator>=(const ParamTag &RHS) const {
            return m_name >= RHS.m_name;
        }

        inline size_t hash() const {
            return std::hash<std::string_view>()(m_name);
        }

    protected:
        std::string_view m_name;
    };

}

namespace std {

    template <>
    struct hash<srt::svs::ParamTag> {
        inline size_t operator()(const srt::svs::ParamTag &key) const {
            return key.hash();
        }
    };

}
