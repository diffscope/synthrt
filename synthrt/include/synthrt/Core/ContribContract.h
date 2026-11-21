#ifndef SYNTHRT_CORE_CONTRIBCONTRACT_H
#define SYNTHRT_CORE_CONTRIBCONTRACT_H

#include <string>
#include <tuple>
#include <utility>

namespace srt {

    /// An owning identity for an interface contract and its API level.
    class ContribContract {
    public:
        ContribContract() = default;

        ContribContract(std::string interfaceId, int level)
            : m_interfaceId(std::move(interfaceId)), m_level(level) {
        }

        /// Returns the identifier serialized as \c interface.
        /// \note The accessor avoids the Windows SDK macro named interface.
        const std::string &interfaceId() const noexcept {
            return m_interfaceId;
        }

        /// Returns the API level of the interface contract.
        int level() const noexcept {
            return m_level;
        }

        /// Renders the contract identity for a diagnostic message.
        std::string toString() const {
            return m_interfaceId + ", level " + std::to_string(m_level);
        }

        bool operator==(const ContribContract &other) const {
            return m_interfaceId == other.m_interfaceId && m_level == other.m_level;
        }

        bool operator!=(const ContribContract &other) const {
            return !(*this == other);
        }

        /// Orders contracts by interface identifier and then API level.
        bool operator<(const ContribContract &other) const {
            return std::tie(m_interfaceId, m_level) < std::tie(other.m_interfaceId, other.m_level);
        }

    private:
        std::string m_interfaceId;
        int m_level = 0;
    };

    /// An owning identity for one implementation variant of a contribution contract.
    /// Equality compares the contract and variant identifier. Compare the contracts explicitly
    /// when implementations of the same contract are interchangeable.
    class ContribContractTriplet {
    public:
        ContribContractTriplet() = default;

        ContribContractTriplet(ContribContract contract, std::string variant)
            : m_contract(std::move(contract)), m_variant(std::move(variant)) {
        }

        ContribContractTriplet(std::string interfaceId, int level, std::string variant)
            : ContribContractTriplet(ContribContract(std::move(interfaceId), level),
                                     std::move(variant)) {
        }

        /// Returns the interface contract implemented by this variant.
        const ContribContract &contract() const noexcept {
            return m_contract;
        }

        /// Returns the interface identifier of the contract.
        const std::string &interfaceId() const noexcept {
            return m_contract.interfaceId();
        }

        /// Returns the API level of the contract.
        int level() const noexcept {
            return m_contract.level();
        }

        /// Returns the implementation identifier serialized as \c variant.
        const std::string &variant() const noexcept {
            return m_variant;
        }

        /// Renders the complete variant identity for a diagnostic message.
        std::string toString() const {
            return m_contract.toString() + ", variant " + m_variant;
        }

        bool operator==(const ContribContractTriplet &other) const {
            return m_contract == other.m_contract && m_variant == other.m_variant;
        }

        bool operator!=(const ContribContractTriplet &other) const {
            return !(*this == other);
        }

        /// Orders identities by contract and then variant identifier.
        bool operator<(const ContribContractTriplet &other) const {
            return std::tie(m_contract, m_variant) < std::tie(other.m_contract, other.m_variant);
        }

    private:
        ContribContract m_contract;
        std::string m_variant;
    };

}

#endif // SYNTHRT_CORE_CONTRIBCONTRACT_H
