#include <memory>
#include <string>
#include <utility>

#include <synthrt/Core/ContribSpecPayload.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

namespace {
    class Payload : public srt::ContribSpecPayload {
    public:
        Payload(srt::ContribContractTriplet triplet, bool &destroyed)
            : ContribSpecPayload(std::move(triplet)), m_destroyed(destroyed) {
        }
        ~Payload() {
            m_destroyed = true;
        }
        int value = 42;

    private:
        bool &m_destroyed;
    };
}

BOOST_AUTO_TEST_CASE(contract_identity_and_polymorphic_ownership) {
    bool destroyed = false;
    srt::ContribContractTriplet original("com.example.Contract", 2, "native");
    std::unique_ptr<srt::ContribSpecPayload> payload =
        std::make_unique<Payload>(original, destroyed);
    original = srt::ContribContractTriplet("com.example.Other", 1, "other");
    BOOST_CHECK(payload->triplet() ==
                srt::ContribContractTriplet("com.example.Contract", 2, "native"));
    BOOST_CHECK_EQUAL(payload->as<Payload>()->value, 42);
    const auto &view = *payload;
    BOOST_CHECK_EQUAL(view.as<Payload>()->value, 42);
    BOOST_CHECK(!destroyed);
    payload.reset();
    BOOST_CHECK(destroyed);
}
