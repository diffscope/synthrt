#ifndef DSINFER_PARAMTAG_H
#define DSINFER_PARAMTAG_H

#include <functional>
#include <string>
#include <utility>

namespace ds {

    class ParamTag {
    public:
        /// Creates an unspecified tag.
        ParamTag() = default;

        /// Creates a tag that owns a name.
        explicit ParamTag(std::string name) : m_name(std::move(name)) {
        }

        /// Returns the parameter name. An empty name denotes an unspecified tag.
        const std::string &name() const noexcept {
            return m_name;
        }

        bool operator==(const ParamTag &RHS) const noexcept {
            return m_name == RHS.m_name;
        }

        bool operator!=(const ParamTag &RHS) const noexcept {
            return m_name != RHS.m_name;
        }

        bool operator<(const ParamTag &RHS) const noexcept {
            return m_name < RHS.m_name;
        }

        bool operator>(const ParamTag &RHS) const noexcept {
            return m_name > RHS.m_name;
        }

        bool operator<=(const ParamTag &RHS) const noexcept {
            return m_name <= RHS.m_name;
        }

        bool operator>=(const ParamTag &RHS) const noexcept {
            return m_name >= RHS.m_name;
        }

    private:
        std::string m_name;
    };

}

namespace std {

    template <>
    struct hash<ds::ParamTag> {
        size_t operator()(const ds::ParamTag &key) const noexcept {
            return hash<string>{}(key.name());
        }
    };

}

#endif // DSINFER_PARAMTAG_H
