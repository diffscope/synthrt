#ifndef SYNTHRT_CORE_CONTRIBTRIPLET_H
#define SYNTHRT_CORE_CONTRIBTRIPLET_H

#include <string>
#include <tuple>
#include <utility>

#include <synthrt/synthrt_global.h>

namespace srt {

    /// The module triplet of a contribution: the interface contract, its API Level and the
    /// implementation variant, which together say what a declaration is.
    ///
    /// A triplet is matched as one value, never field by field. A Level does not serve another
    /// Level, and a variant belongs to the interface it is declared under, so two triplets that
    /// differ in any field name different implementations. The fields follow the order of the
    /// specification: interface, level, variant.
    class ContribTriplet {
    public:
        ContribTriplet() = default;

        ContribTriplet(std::string interfaceId, int level, std::string variant)
            : m_interfaceId(std::move(interfaceId)), m_level(level), m_variant(std::move(variant)) {
        }

        /// Returns the contract identifier serialized as \c interface.
        ///
        /// \note Not named after the field it carries. The Windows SDK defines \c interface as a
        ///       macro for \c struct in \c combaseapi.h, unconditionally, so a member of that name
        ///       fails to compile in any translation unit that has reached a COM header. Pushing
        ///       and popping the macro around this header would not help, because the call sites
        ///       are in callers' translation units. The serialized field keeps its own name.
        const std::string &interfaceId() const {
            return m_interfaceId;
        }

        /// Returns the API Level of the interface contract.
        int level() const {
            return m_level;
        }

        /// Returns the implementation variant within the interface contract.
        const std::string &variant() const {
            return m_variant;
        }

        /// Renders this value for a message, as \c "<interface> level <level> variant <variant>".
        std::string toString() const {
            return m_interfaceId + " level " + std::to_string(m_level) + " variant " + m_variant;
        }

        bool operator==(const ContribTriplet &other) const {
            return m_interfaceId == other.m_interfaceId && m_level == other.m_level &&
                   m_variant == other.m_variant;
        }

        bool operator!=(const ContribTriplet &other) const {
            return !(*this == other);
        }

        /// Orders triplets field by field, so that a triplet can key an ordered container.
        bool operator<(const ContribTriplet &other) const {
            return std::tie(m_interfaceId, m_level, m_variant) <
                   std::tie(other.m_interfaceId, other.m_level, other.m_variant);
        }

    private:
        std::string m_interfaceId;
        int m_level = 0;
        std::string m_variant;
    };

}

#endif // SYNTHRT_CORE_CONTRIBTRIPLET_H
