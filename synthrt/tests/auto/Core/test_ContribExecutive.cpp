#define BOOST_TEST_MAIN
#include "TestSupport.h"

#include <cstdlib>

BOOST_AUTO_TEST_CASE(null_children_and_missing_import_roles_are_rejected) {
    TemporaryDirectory temporary;
    auto unit = makeUnit();
    auto loaded =
        unit.openPackage(writePackage(temporary.path(), "root", "root", "1"), srt::SynthUnit::Load);
    BOOST_REQUIRE(loaded);
    auto package = loaded.take();
    auto spec = package.contribution(testCategoryName, "main");
    BOOST_REQUIRE(spec);
    TestExecutive parent(*spec);
    BOOST_CHECK(parent.parent() == nullptr);
    BOOST_CHECK(parent.children().empty());
    auto adopted = parent.adopt(nullptr);
    BOOST_REQUIRE(!adopted);
    BOOST_CHECK(adopted.error().code() == srt::Error::InvalidArgument);
    for (const auto role : {"", "unknown"}) {
        auto created = parent.create(role, TestRuntimeOptions());
        BOOST_REQUIRE(!created);
        BOOST_CHECK(created.error().code() == srt::Error::InvalidArgument);
    }
    BOOST_CHECK(parent.children().empty());
    BOOST_CHECK(parent.lifecycleState() == srt::ContribExecutive::Running);
}

BOOST_AUTO_TEST_CASE(cross_unit_adoption_destroys_the_rejected_child) {
    TemporaryDirectory temporary;
    auto firstUnit = makeUnit();
    auto secondUnit = makeUnit();
    const auto path = writePackage(temporary.path(), "root", "root", "1");
    auto first = firstUnit.openPackage(path, srt::SynthUnit::Load);
    auto second = secondUnit.openPackage(path, srt::SynthUnit::Load);
    BOOST_REQUIRE(first);
    BOOST_REQUIRE(second);
    int destroyed = 0;
    TestExecutive parent(*first->contribution(testCategoryName, "main"));
    auto adopted = parent.adopt(std::make_unique<TestExecutive>(
        *second->contribution(testCategoryName, "main"), &destroyed));
    BOOST_REQUIRE(!adopted);
    BOOST_CHECK(adopted.error().code() == srt::Error::InvalidArgument);
    BOOST_CHECK_EQUAL(destroyed, 1);
    BOOST_CHECK(parent.children().empty());
    BOOST_CHECK(&parent.synthUnit() == &firstUnit);
}

BOOST_AUTO_TEST_CASE(child_snapshots_survive_detachment_and_vacated_slots_are_reused) {
    TemporaryDirectory temporary;
    auto unit = makeUnit();
    auto first = unit.openPackage(writePackage(temporary.path(), "first", "first", "1"),
                                  srt::SynthUnit::Load);
    auto second = unit.openPackage(writePackage(temporary.path(), "second", "second", "1"),
                                   srt::SynthUnit::Load);
    BOOST_REQUIRE(first);
    BOOST_REQUIRE(second);
    auto spec = second->contribution(testCategoryName, "main");
    BOOST_REQUIRE(spec);
    int destroyed = 0;
    TestExecutive parent(*first->contribution(testCategoryName, "main"));
    auto a = parent.adopt(std::make_unique<TestExecutive>(*spec, &destroyed));
    auto b = parent.adopt(std::make_unique<TestExecutive>(*spec, &destroyed));
    BOOST_REQUIRE(a);
    BOOST_REQUIRE(b);
    const auto snapshot = parent.children();
    BOOST_REQUIRE_EQUAL(snapshot.size(), 2u);
    BOOST_CHECK(snapshot[0] == *a);
    BOOST_CHECK(snapshot[1] == *b);
    BOOST_CHECK((*a)->parent() == &parent);
    BOOST_CHECK(&(*a)->spec() == spec);
    delete *a;
    BOOST_CHECK_EQUAL(destroyed, 1);
    const auto remaining = parent.children();
    BOOST_REQUIRE_EQUAL(remaining.size(), 1u);
    BOOST_CHECK(remaining[0] == *b);
    BOOST_CHECK_EQUAL(snapshot.size(), 2u);
    auto replacement = parent.adopt(std::make_unique<TestExecutive>(*spec, &destroyed));
    BOOST_REQUIRE(replacement);
    const auto replaced = parent.children();
    BOOST_REQUIRE_EQUAL(replaced.size(), 2u);
    BOOST_CHECK(replaced[0] == *replacement);
    BOOST_CHECK(replaced[1] == *b);
    delete *b;
    delete *replacement;
    BOOST_CHECK_EQUAL(destroyed, 3);
    BOOST_CHECK(parent.children().empty());
}

BOOST_AUTO_TEST_CASE(deleting_a_subtree_detaches_it_and_destroys_every_descendant_once) {
    TemporaryDirectory temporary;
    auto unit = makeUnit();
    auto package =
        unit.openPackage(writePackage(temporary.path(), "root", "root", "1"), srt::SynthUnit::Load);
    BOOST_REQUIRE(package);
    auto spec = package->contribution(testCategoryName, "main");
    BOOST_REQUIRE(spec);
    int destroyed = 0;
    {
        TestExecutive root(*spec, &destroyed);
        auto child = root.adopt(std::make_unique<TestExecutive>(*spec, &destroyed));
        auto sibling = root.adopt(std::make_unique<TestExecutive>(*spec, &destroyed));
        BOOST_REQUIRE(child);
        BOOST_REQUIRE(sibling);
        auto grandchild = (*child)->adopt(std::make_unique<TestExecutive>(*spec, &destroyed));
        BOOST_REQUIRE(grandchild);
        BOOST_CHECK((*grandchild)->parent() == *child);
        BOOST_CHECK_EQUAL(root.children().size(), 2u);
        BOOST_CHECK_EQUAL((*child)->children().size(), 1u);
        delete *child;
        BOOST_CHECK_EQUAL(destroyed, 2);
        const auto remaining = root.children();
        BOOST_REQUIRE_EQUAL(remaining.size(), 1u);
        BOOST_CHECK(remaining[0] == *sibling);
        BOOST_CHECK((*sibling)->parent() == &root);
    }
    BOOST_CHECK_EQUAL(destroyed, 4);
    BOOST_CHECK_EQUAL(unit.loadedPackages().size(), 1u);
    package->reset();
    BOOST_CHECK(unit.loadedPackages().empty());
}

BOOST_AUTO_TEST_CASE(executive_lifetime_violation_is_fatal) {
    const auto violation = std::getenv("SYNTHRT_TEST_FATAL_LIFETIME");
    if (!violation) {
        return;
    }

    TemporaryDirectory temporary;
    auto unit = std::make_unique<ConfiguredSynthUnit>();
    auto package = unit->openPackage(writePackage(temporary.path(), "root", "root", "1"),
                                     srt::SynthUnit::Load);
    BOOST_REQUIRE(package);
    auto spec = package->contribution(testCategoryName, "main");
    BOOST_REQUIRE(spec);
    auto executive = std::make_unique<TestExecutive>(*spec);

    if (std::string(violation) == "package") {
        package->reset();
        BOOST_FAIL("releasing the last PackageHandle must be fatal");
    }
    if (std::string(violation) == "unit") {
        unit.reset();
        BOOST_FAIL("destroying SynthUnit must be fatal");
    }
    BOOST_FAIL("unknown SYNTHRT_TEST_FATAL_LIFETIME value");
}
