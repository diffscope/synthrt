#include <synthrt/Core/SynthUnit.h>
#include <synthrt/Core/Contribute.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

using srt::ContribCategory;
using srt::SynthUnit;

BOOST_AUTO_TEST_SUITE(test_SynthUnit)

// The categories synthrt registers for itself have to be there.
//
// This is the regression test for the factory list having been a static member written from other
// translation units: a registration arriving before the container constructed itself was discarded,
// and the only symptom was a category that silently never existed.
BOOST_AUTO_TEST_CASE(test_SynthUnit_RegisteredCategories) {
    SynthUnit su;

    auto singer = su.category("singer");
    BOOST_REQUIRE(singer != nullptr);
    BOOST_CHECK(singer->name() == "singer");

    auto inference = su.category("inference");
    BOOST_REQUIRE(inference != nullptr);
    BOOST_CHECK(inference->name() == "inference");

    BOOST_CHECK(singer != inference);
    BOOST_CHECK(su.category("nonexistent") == nullptr);
    BOOST_CHECK(su.category("") == nullptr);
}

// Registration is process-wide, the instances are not: every unit builds its own set from the same
// factories.
BOOST_AUTO_TEST_CASE(test_SynthUnit_CategoriesArePerUnit) {
    SynthUnit first;
    SynthUnit second;

    auto a = first.category("singer");
    auto b = second.category("singer");

    BOOST_REQUIRE(a != nullptr);
    BOOST_REQUIRE(b != nullptr);
    BOOST_CHECK(a != b);
    BOOST_CHECK(a->name() == b->name());
    BOOST_CHECK(a->SU() == &first);
    BOOST_CHECK(b->SU() == &second);
}

// Constructing and destroying a unit repeatedly must keep working, since the factory list outlives
// every one of them.
BOOST_AUTO_TEST_CASE(test_SynthUnit_RepeatedConstruction) {
    for (int i = 0; i < 3; ++i) {
        SynthUnit su;
        BOOST_CHECK(su.category("singer") != nullptr);
        BOOST_CHECK(su.category("inference") != nullptr);
    }
}

BOOST_AUTO_TEST_SUITE_END()
