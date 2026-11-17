#define BOOST_TEST_MAIN
#include "TestSupport.h"

BOOST_AUTO_TEST_CASE(test_shared_dependency_lives_until_every_dependent_is_released) {
    TemporaryDirectory temporary;
    const auto packages = temporary.path() / "packages";
    writePackage(packages, "dep", "dep", "1", "1");
    const auto first = writePackage(temporary.path(), "first", "first", "1", {},
                                    R"([{"id":"dep","version":"1"}])");
    const auto second = writePackage(temporary.path(), "second", "second", "1", {},
                                     R"([{"id":"dep","version":"1"}])");

    auto unit = makeUnit();
    const std::vector<fs::path> paths = {packages};
    unit.setPackagePaths(paths);
    auto firstResult = unit.openPackage(first, srt::SynthUnit::Load);
    BOOST_REQUIRE(firstResult);
    auto firstPackage = firstResult.take();
    auto secondResult = unit.openPackage(second, srt::SynthUnit::Load);
    BOOST_REQUIRE(secondResult);
    auto secondPackage = secondResult.take();
    BOOST_CHECK_EQUAL(unit.loadedPackages().size(), 3u);

    firstPackage.reset();
    BOOST_CHECK_EQUAL(unit.loadedPackages().size(), 2u);
    BOOST_CHECK(unit.findLoadedPackage("dep", stdc::VersionNumber(1)).has_value());

    secondPackage.reset();
    BOOST_CHECK(unit.loadedPackages().empty());
    BOOST_CHECK(unit.category(testCategoryName)->contributions().empty());
}
BOOST_AUTO_TEST_CASE(dependency_identity_includes_version) {
    const srt::PackageDependency first("voice", stdc::VersionNumber(1, 0));
    const srt::PackageDependency same("voice", stdc::VersionNumber(1, 0));
    const srt::PackageDependency otherVersion("voice", stdc::VersionNumber(2, 0));
    const srt::PackageDependency otherId("other", stdc::VersionNumber(1, 0));
    BOOST_CHECK(first == same);
    BOOST_CHECK(first != otherVersion);
    BOOST_CHECK(first != otherId);
    srt::PackageHandle empty;
    BOOST_CHECK(!empty);
    auto copy = empty;
    BOOST_CHECK(!copy);
    auto moved = std::move(copy);
    BOOST_CHECK(!moved);
    moved.reset();
    BOOST_CHECK(!moved);
}
