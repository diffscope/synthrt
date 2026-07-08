#ifndef PARAMTAG_H
#define PARAMTAG_H

#include <string_view>
#include <type_traits>

namespace srt::svs {

    class ParamTag {
    public:
        inline constexpr ParamTag() = default;

        template <size_t N>
        inline constexpr ParamTag(const char (&name)[N]) : _name(name, N - 1) {
        }

        inline constexpr std::string_view name() const {
            return _name;
        }

        inline bool operator==(const ParamTag &RHS) const {
            return _name == RHS._name;
        }

        inline bool operator!=(const ParamTag &RHS) const {
            return _name != RHS._name;
        }

        inline bool operator<(const ParamTag &RHS) const {
            return _name < RHS._name;
        }

        inline bool operator>(const ParamTag &RHS) const {
            return _name > RHS._name;
        }

        inline bool operator<=(const ParamTag &RHS) const {
            return _name <= RHS._name;
        }

        inline bool operator>=(const ParamTag &RHS) const {
            return _name >= RHS._name;
        }

        inline size_t hash() const {
            return std::hash<std::string_view>()(_name);
        }

    protected:
        std::string_view _name;
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

#endif // PARAMTAG_H