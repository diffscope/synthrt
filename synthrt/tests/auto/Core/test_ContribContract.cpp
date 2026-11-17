#include <synthrt/Core/ContribContract.h>

#include <set>
#include <type_traits>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(test_ContribContract)

BOOST_AUTO_TEST_CASE(test_contract_identity_and_ownership) {
    std::string interfaceId = "com.example.Contract";
    const srt::ContribContract contract(interfaceId, 2);
    interfaceId.assign("com.example.Other");
    BOOST_CHECK_EQUAL(contract.interfaceId(), "com.example.Contract");
    BOOST_CHECK_EQUAL(contract.level(), 2);
    BOOST_CHECK_EQUAL(contract.toString(), "com.example.Contract, level 2");
    BOOST_CHECK(contract == srt::ContribContract("com.example.Contract", 2));
    BOOST_CHECK(contract != srt::ContribContract("com.example.Other", 2));
    BOOST_CHECK(contract != srt::ContribContract("com.example.Contract", 3));

    const std::set<srt::ContribContract> contracts{
        contract, {"com.example.Contract", 1},
         {"com.example.Other",    2},
         contract
    };
    BOOST_CHECK_EQUAL(contracts.size(), 3);
    BOOST_CHECK(*contracts.begin() == srt::ContribContract("com.example.Contract", 1));
    BOOST_CHECK(*contracts.rbegin() == srt::ContribContract("com.example.Other", 2));
}

BOOST_AUTO_TEST_CASE(test_variant_preserves_contract_and_owns_identifiers) {
    srt::ContribContract contract("com.example.Contract", 2);
    std::string variantId = "native";
    const srt::ContribContractTriplet native(contract, variantId);
    const srt::ContribContractTriplet remote(contract, "remote");
    contract = srt::ContribContract("com.example.Other", 3);
    variantId.assign("changed");
    BOOST_CHECK_EQUAL(native.interfaceId(), "com.example.Contract");
    BOOST_CHECK_EQUAL(native.level(), 2);
    BOOST_CHECK_EQUAL(native.variant(), "native");
    BOOST_CHECK(native.contract() == remote.contract());
    BOOST_CHECK(native != remote);
    BOOST_CHECK(native == srt::ContribContractTriplet("com.example.Contract", 2, "native"));
    BOOST_CHECK(!(std::is_convertible_v<srt::ContribContractTriplet, srt::ContribContract>) );
    BOOST_CHECK(!(std::is_convertible_v<srt::ContribContract, srt::ContribContractTriplet>) );
}

BOOST_AUTO_TEST_CASE(test_default_identities) {
    const srt::ContribContract contract;
    const srt::ContribContractTriplet variant;
    BOOST_CHECK(contract.interfaceId().empty());
    BOOST_CHECK_EQUAL(contract.level(), 0);
    BOOST_CHECK(variant.contract() == contract);
    BOOST_CHECK(variant.variant().empty());
}

BOOST_AUTO_TEST_CASE(test_triplet_compares_as_one_value) {
    const srt::ContribContractTriplet triplet("com.example.Contract", 2, "native");
    BOOST_CHECK(triplet == srt::ContribContractTriplet("com.example.Contract", 2, "native"));
    BOOST_CHECK_EQUAL(triplet.toString(), "com.example.Contract, level 2, variant native");

    // Each field alone tells two triplets apart, and the order is total, so a triplet can key a
    // set.
    const srt::ContribContractTriplet others[] = {
        {"com.example.Other",    2, "native"},
        {"com.example.Contract", 1, "native"},
        {"com.example.Contract", 2, "remote"},
    };
    for (const auto &other : others) {
        BOOST_TEST_CONTEXT(other.toString()) {
            BOOST_CHECK(triplet != other);
            BOOST_CHECK((triplet < other) != (other < triplet));
        }
    }
    BOOST_CHECK(!(triplet < triplet));
}

BOOST_AUTO_TEST_SUITE_END()
