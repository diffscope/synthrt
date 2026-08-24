#include <string>
#include <type_traits>
#include <unordered_set>

#include <dsinfer/Core/ParamTag.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

using ds::ParamTag;

BOOST_AUTO_TEST_SUITE(test_ParamTag)

BOOST_AUTO_TEST_CASE(test_OwnsName) {
    std::string name = "pitch";
    ParamTag tag(name);
    name.assign("changed");
    BOOST_CHECK(tag.name() == "pitch");
}

BOOST_AUTO_TEST_CASE(test_IsRegularValue) {
    static_assert(std::is_copy_constructible_v<ParamTag>);
    static_assert(std::is_copy_assignable_v<ParamTag>);
    static_assert(std::is_move_constructible_v<ParamTag>);
    static_assert(std::is_move_assignable_v<ParamTag>);

    ParamTag tag;
    BOOST_CHECK(tag.name().empty());

    tag = ParamTag("energy");
    BOOST_CHECK(tag.name() == "energy");
}

BOOST_AUTO_TEST_CASE(test_ComparisonAndHashUseName) {
    const ParamTag energy("energy");
    const ParamTag pitch("pitch");
    const ParamTag anotherPitch(std::string("pitch"));

    BOOST_CHECK(energy < pitch);
    BOOST_CHECK(pitch == anotherPitch);
    BOOST_CHECK(!(pitch != anotherPitch));

    const std::unordered_set<ParamTag> tags{energy, pitch, anotherPitch};
    BOOST_CHECK(tags.size() == 2);
    BOOST_CHECK(tags.count(ParamTag("pitch")) == 1);
}

BOOST_AUTO_TEST_SUITE_END()
