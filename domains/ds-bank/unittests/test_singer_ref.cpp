// SingerRef 单元测试。
//
// 覆盖 d:\projects\synthrt\domains\ds-bank\lib\SingerRef.cpp 的 toString/parse：
//   - toString：基本格式 "packageId:singerId"
//   - parse：合法/非法格式、无分隔符、空字符串
//   - round-trip：toString(parse(s)) == s
//   - version 字段不参与 toString（仅 packageId:singerId）
//   - 包含 Unicode 字符的 packageId/singerId
//   - 包含特殊字符（点、连字符、下划线）
//
// 设计原则对应：
//   ROBUST-05: parse 在无分隔符时不抛异常，返回空 singerId

#include <string>

#include <catch2/catch_test_macros.hpp>

#include <diffsinger/Bank/SingerRef.h>

using namespace ds::bank;

// ===========================================================================
// toString：基本格式
// ===========================================================================

TEST_CASE("SingerRef toString produces 'packageId:singerId'", "[ds-bank][singer-ref]") {
    SingerRef ref{"pkg.a", "singer_x"};
    REQUIRE(ref.toString() == "pkg.a:singer_x");
}

TEST_CASE("SingerRef toString with version still omits version",
          "[ds-bank][singer-ref]") {
    // v2: version 字段不参与 toString()，保持 "packageId:singerId" 格式
    SingerRef ref{"pkg.a", "singer_x", "1.0.0"};
    REQUIRE(ref.toString() == "pkg.a:singer_x");
    REQUIRE(ref.version == "1.0.0");
}

TEST_CASE("SingerRef toString with empty fields", "[ds-bank][singer-ref]") {
    SingerRef empty{};
    REQUIRE(empty.toString() == ":");
    REQUIRE(empty.packageId.empty());
    REQUIRE(empty.singerId.empty());

    SingerRef onlyPkg{"pkg.only", ""};
    REQUIRE(onlyPkg.toString() == "pkg.only:");

    SingerRef onlySinger{"", "singer_only"};
    REQUIRE(onlySinger.toString() == ":singer_only");
}

// ===========================================================================
// parse：合法格式
// ===========================================================================

TEST_CASE("SingerRef parse extracts packageId and singerId",
          "[ds-bank][singer-ref][parse]") {
    auto ref = SingerRef::parse("pkg.a:singer_x");
    REQUIRE(ref.packageId == "pkg.a");
    REQUIRE(ref.singerId == "singer_x");
    REQUIRE(ref.version.empty());  // parse 不设置 version
}

TEST_CASE("SingerRef parse handles unicode packageId and singerId",
          "[ds-bank][singer-ref][parse][unicode]") {
    auto ref = SingerRef::parse("歌手.中文:声库_主唱");
    REQUIRE(ref.packageId == "歌手.中文");
    REQUIRE(ref.singerId == "声库_主唱");
}

TEST_CASE("SingerRef parse handles special characters in ids",
          "[ds-bank][singer-ref][parse]") {
    // 点、连字符、下划线、数字
    auto ref = SingerRef::parse("org.diffinger.opencpop-v2:singer_01");
    REQUIRE(ref.packageId == "org.diffinger.opencpop-v2");
    REQUIRE(ref.singerId == "singer_01");
}

// ===========================================================================
// parse：边缘场景
// ===========================================================================

TEST_CASE("SingerRef parse with no separator treats whole string as packageId",
          "[ds-bank][singer-ref][parse][edge]") {
    // 没有冒号：整体作为 packageId，singerId 留空
    auto ref = SingerRef::parse("no_separator");
    REQUIRE(ref.packageId == "no_separator");
    REQUIRE(ref.singerId.empty());
}

TEST_CASE("SingerRef parse empty string yields empty ref",
          "[ds-bank][singer-ref][parse][edge]") {
    auto ref = SingerRef::parse("");
    REQUIRE(ref.packageId.empty());
    REQUIRE(ref.singerId.empty());
}

TEST_CASE("SingerRef parse with leading separator yields empty packageId",
          "[ds-bank][singer-ref][parse][edge]") {
    auto ref = SingerRef::parse(":singer_only");
    REQUIRE(ref.packageId.empty());
    REQUIRE(ref.singerId == "singer_only");
}

TEST_CASE("SingerRef parse with trailing separator yields empty singerId",
          "[ds-bank][singer-ref][parse][edge]") {
    auto ref = SingerRef::parse("pkg.only:");
    REQUIRE(ref.packageId == "pkg.only");
    REQUIRE(ref.singerId.empty());
}

TEST_CASE("SingerRef parse with multiple separators splits on first",
          "[ds-bank][singer-ref][parse][edge]") {
    // 多个冒号：只在第一个冒号分割
    auto ref = SingerRef::parse("pkg.a:singer:extra");
    REQUIRE(ref.packageId == "pkg.a");
    REQUIRE(ref.singerId == "singer:extra");
}

// ===========================================================================
// Round-trip: toString(parse(s)) == s
// ===========================================================================

TEST_CASE("SingerRef round-trip toString(parse(s)) preserves string",
          "[ds-bank][singer-ref][round-trip]") {
    const std::vector<std::string> cases = {
        "pkg.a:singer_x",
        "org.diffinger.opencpop:singer_01",
        "歌手.中文:声库_主唱",
        "pkg.with.dots.and-hyphens:singer_with_underscores",
    };
    for (const auto &s : cases) {
        auto ref = SingerRef::parse(s);
        REQUIRE(ref.toString() == s);
    }
}

// ===========================================================================
// 构造函数重载
// ===========================================================================

TEST_CASE("SingerRef two-arg constructor leaves version empty",
          "[ds-bank][singer-ref]") {
    SingerRef ref{"pkg.a", "singer_x"};
    REQUIRE(ref.packageId == "pkg.a");
    REQUIRE(ref.singerId == "singer_x");
    REQUIRE(ref.version.empty());
}

TEST_CASE("SingerRef three-arg constructor sets version", "[ds-bank][singer-ref]") {
    SingerRef ref{"pkg.a", "singer_x", "1.2.3"};
    REQUIRE(ref.packageId == "pkg.a");
    REQUIRE(ref.singerId == "singer_x");
    REQUIRE(ref.version == "1.2.3");
}

TEST_CASE("SingerRef default constructor leaves all fields empty",
          "[ds-bank][singer-ref]") {
    SingerRef ref;
    REQUIRE(ref.packageId.empty());
    REQUIRE(ref.singerId.empty());
    REQUIRE(ref.version.empty());
}
