// VersionUtils 极端场景测试
//
// 覆盖 D-18 人工决策约束：VersionUtils 必须处理 5+ 段版本号不栈溢出，
// 空 QVersionNumber 必须映射到 stdc::VersionNumber() 而非 0.0.0。
//
// 测试目标：
//   - normalizeVersion: v/V 前缀、'-' 截断、5+ 段、leading/trailing dots、空串
//   - compareVersions: 非数字段、空串、不同段数
//   - VersionRange: 多约束交集、reversed hyphen、COMPATIBLE 单段 (~1)、parseError
//
// 这些用例来自 docs/decisions/human-decisions.md D-18 与 v3 P3 VersionUtils 修复
// 说明。源码实现见 lib/Core/Dependency/VersionUtils.cpp。

#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <synthrt/Core/Dependency/VersionUtils.h>

using srt::dependency::VersionRange;

// ---------------------------------------------------------------------------
// normalizeVersion: v/V 前缀剥离
// ---------------------------------------------------------------------------
TEST_CASE("normalizeVersion strips v/V prefix", "[version][normalize]") {
    SECTION("lowercase v prefix") {
        REQUIRE(VersionRange::normalizeVersion("v1.2.3") == "1.2.3");
    }
    SECTION("uppercase V prefix") {
        REQUIRE(VersionRange::normalizeVersion("V1.2.3") == "1.2.3");
    }
    SECTION("no prefix stays as-is") {
        REQUIRE(VersionRange::normalizeVersion("1.2.3") == "1.2.3");
    }
}

// ---------------------------------------------------------------------------
// normalizeVersion: '-' 截断 (pre-release suffix)
// ---------------------------------------------------------------------------
TEST_CASE("normalizeVersion truncates at dash", "[version][normalize]") {
    SECTION("pre-release suffix truncated") {
        REQUIRE(VersionRange::normalizeVersion("1.2.3-alpha") == "1.2.3");
    }
    SECTION("build metadata digits absorbed then truncated to 3 segments") {
        // 实现按 '.' 分段后逐段提取数字字符："1.2.3+build.7" → ["1","2","3","7"]
        // resize(3) 截断到 3 段 → "1.2.3"
        REQUIRE(VersionRange::normalizeVersion("1.2.3+build.7") == "1.2.3");
    }
    SECTION("v prefix and dash combined") {
        REQUIRE(VersionRange::normalizeVersion("v2.0.0-rc.1") == "2.0.0");
    }
}

// ---------------------------------------------------------------------------
// D-18: 5+ 段版本号不栈溢出
// ---------------------------------------------------------------------------
TEST_CASE("normalizeVersion handles 5+ segment versions without stack overflow",
          "[version][normalize][d-18]") {
    SECTION("4 segments truncated to 3") {
        REQUIRE(VersionRange::normalizeVersion("1.2.3.4") == "1.2.3");
    }
    SECTION("5 segments truncated to 3") {
        REQUIRE(VersionRange::normalizeVersion("1.2.3.4.5") == "1.2.3");
    }
    SECTION("6 segments truncated to 3") {
        REQUIRE(VersionRange::normalizeVersion("10.20.30.40.50.60") == "10.20.30");
    }
    SECTION("very long version does not crash") {
        // 构造 20 段版本号，验证不发生栈溢出或异常
        std::string longVer;
        for (int i = 1; i <= 20; ++i) {
            if (i > 1) longVer += ".";
            longVer += std::to_string(i);
        }
        auto result = VersionRange::normalizeVersion(longVer);
        REQUIRE(result == "1.2.3");
    }
}

// ---------------------------------------------------------------------------
// normalizeVersion: 段数不足补 0
// ---------------------------------------------------------------------------
TEST_CASE("normalizeVersion pads short versions to 3 segments", "[version][normalize]") {
    SECTION("single segment") {
        REQUIRE(VersionRange::normalizeVersion("1") == "1.0.0");
    }
    SECTION("two segments") {
        REQUIRE(VersionRange::normalizeVersion("1.2") == "1.2.0");
    }
    SECTION("empty string returns 0.0.0") {
        REQUIRE(VersionRange::normalizeVersion("") == "0.0.0");
    }
}

// ---------------------------------------------------------------------------
// normalizeVersion: 非数字字符处理
// ---------------------------------------------------------------------------
TEST_CASE("normalizeVersion extracts digit characters from segments",
          "[version][normalize]") {
    SECTION("digit-only chars kept") {
        // "v1.2.x" 中 'x' 被忽略，留下空段被丢弃
        auto r = VersionRange::normalizeVersion("v1.2.x");
        // 解析：剥离 v -> "1.2.x"，按 '.' 分段 -> ["1","2","x"]
        // 每段提取数字：["1","2",""]，丢弃空段 -> ["1","2"]
        // 补 0 -> ["1","2","0"] -> "1.2.0"
        REQUIRE(r == "1.2.0");
    }
    SECTION("all non-digit segments collapse to 0.0.0") {
        REQUIRE(VersionRange::normalizeVersion("abc.def") == "0.0.0");
    }
}

// ---------------------------------------------------------------------------
// compareVersions
// ---------------------------------------------------------------------------
TEST_CASE("compareVersions basic ordering", "[version][compare]") {
    REQUIRE(VersionRange::compareVersions("1.0.0", "1.0.0") == 0);
    REQUIRE(VersionRange::compareVersions("1.0.0", "2.0.0") < 0);
    REQUIRE(VersionRange::compareVersions("2.0.0", "1.0.0") > 0);
    REQUIRE(VersionRange::compareVersions("1.0.0", "1.0.1") < 0);
    REQUIRE(VersionRange::compareVersions("1.0.1", "1.0.0") > 0);
}

TEST_CASE("compareVersions handles different segment counts", "[version][compare]") {
    SECTION("shorter version padded with zeros") {
        REQUIRE(VersionRange::compareVersions("1.0", "1.0.0") == 0);
        REQUIRE(VersionRange::compareVersions("1", "1.0.0") == 0);
    }
    SECTION("longer version with nonzero trailing segment") {
        REQUIRE(VersionRange::compareVersions("1.0.0.1", "1.0.0") > 0);
    }
}

TEST_CASE("compareVersions non-numeric segments treated as 0", "[version][compare]") {
    SECTION("non-numeric segment equals 0") {
        REQUIRE(VersionRange::compareVersions("1.x.0", "1.0.0") == 0);
    }
    SECTION("empty string equals 0.0.0") {
        REQUIRE(VersionRange::compareVersions("", "0.0.0") == 0);
        REQUIRE(VersionRange::compareVersions("", "") == 0);
    }
}

// ---------------------------------------------------------------------------
// VersionRange: 空串和通配符
// ---------------------------------------------------------------------------
TEST_CASE("VersionRange empty string and wildcard produce ANY constraint",
          "[version][range]") {
    SECTION("empty string") {
        VersionRange r("");
        REQUIRE(r.valid());
        REQUIRE(r.toString() == "*");
    }
    SECTION("wildcard *") {
        VersionRange r("*");
        REQUIRE(r.valid());
        REQUIRE(r.toString() == "*");
    }
}

// ---------------------------------------------------------------------------
// VersionRange: COMPATIBLE (~) 操作符
// ---------------------------------------------------------------------------
TEST_CASE("VersionRange COMPATIBLE operator matches major.minor and patch >=",
          "[version][range][compatible]") {
    SECTION("~1.2.3 matches same major.minor with patch >=") {
        VersionRange r("~1.2.3");
        REQUIRE(r.valid());
        REQUIRE(r.toString() == "~1.2.3");

        std::vector<std::string> avail = {"1.2.3", "1.2.5", "1.3.0", "1.1.9"};
        auto matched = r.getVersionsInRange(avail);
        REQUIRE(matched.size() == 2);
        // 排序为降序，所以 [0] 是 1.2.5
        REQUIRE(matched[0] == "1.2.5");
        REQUIRE(matched[1] == "1.2.3");
    }
    SECTION("~1.0.0 does not match 1.1.0") {
        VersionRange r("~1.0.0");
        std::vector<std::string> avail = {"1.0.0", "1.0.5", "1.1.0", "2.0.0"};
        auto matched = r.getVersionsInRange(avail);
        REQUIRE(matched.size() == 2); // 1.0.5, 1.0.0
    }
}

TEST_CASE("VersionRange COMPATIBLE with single segment (~1)",
          "[version][range][compatible]") {
    // ~1 是合法输入；compareVersions 把 1 等同于 1.0.0
    SECTION("~1 matches 1.x.x where patch >= 0") {
        VersionRange r("~1");
        REQUIRE(r.valid());
        // ~1 normalizeVersion 后变成 "1.0.0"，targetParts = ["1","0","0"]
        // 1.0.0 / 1.0.5 应匹配；1.1.0 不匹配（minor 不同）
        std::vector<std::string> avail = {"1.0.0", "1.0.5", "1.1.0"};
        auto matched = r.getVersionsInRange(avail);
        REQUIRE(matched.size() == 2);
    }
}

// ---------------------------------------------------------------------------
// VersionRange: HYPHEN_RANGE
// ---------------------------------------------------------------------------
TEST_CASE("VersionRange hyphen range matches inclusive bounds",
          "[version][range][hyphen]") {
    SECTION("1.0.0 - 2.0.0 inclusive") {
        VersionRange r("1.0.0 - 2.0.0");
        REQUIRE(r.valid());
        REQUIRE(r.toString() == "1.0.0-2.0.0");

        std::vector<std::string> avail = {"0.9.0", "1.0.0", "1.5.0", "2.0.0", "2.1.0"};
        auto matched = r.getVersionsInRange(avail);
        REQUIRE(matched.size() == 3); // 2.0.0, 1.5.0, 1.0.0
        REQUIRE(matched[0] == "2.0.0");
        REQUIRE(matched[2] == "1.0.0");
    }
}

TEST_CASE("VersionRange reversed hyphen range produces parseError or empty match",
          "[version][range][hyphen][reversed]") {
    // 2.0.0 - 1.0.0 是 reversed hyphen range；当前实现不显式校验 reversed，
    // 但 getVersionsInRange 会返回空（因为没有任何版本能同时 >= 2.0.0 且 <= 1.0.0）。
    SECTION("reversed bounds yield empty match") {
        VersionRange r("2.0.0 - 1.0.0");
        REQUIRE(r.valid());
        std::vector<std::string> avail = {"1.0.0", "1.5.0", "2.0.0"};
        auto matched = r.getVersionsInRange(avail);
        REQUIRE(matched.empty());
    }
}

// ---------------------------------------------------------------------------
// VersionRange: 多约束（空格分隔）
// ---------------------------------------------------------------------------
TEST_CASE("VersionRange multi-constraint intersection", "[version][range][multi]") {
    SECTION(">=1.0.0 <2.0.0 intersection") {
        VersionRange r(">=1.0.0 <2.0.0");
        REQUIRE(r.valid());
        REQUIRE(r.toString() == ">=1.0.0 <2.0.0");

        std::vector<std::string> avail = {"0.9.0", "1.0.0", "1.5.0", "2.0.0", "2.1.0"};
        auto matched = r.getVersionsInRange(avail);
        REQUIRE(matched.size() == 2); // 1.5.0, 1.0.0
        REQUIRE(matched[0] == "1.5.0");
        REQUIRE(matched[1] == "1.0.0");
    }
    SECTION(">1.0 <=1.5 intersection") {
        VersionRange r(">1.0 <=1.5");
        std::vector<std::string> avail = {"1.0", "1.2", "1.5", "1.6"};
        auto matched = r.getVersionsInRange(avail);
        REQUIRE(matched.size() == 2); // 1.5, 1.2
    }
    SECTION("empty intersection yields empty result") {
        VersionRange r(">=2.0.0 <1.0.0");
        REQUIRE(r.valid());
        std::vector<std::string> avail = {"0.5.0", "1.0.0", "1.5.0", "2.0.0"};
        auto matched = r.getVersionsInRange(avail);
        REQUIRE(matched.empty());
    }
}

// ---------------------------------------------------------------------------
// VersionRange: parseError
// ---------------------------------------------------------------------------
TEST_CASE("VersionRange invalid constraint produces parseError",
          "[version][range][error]") {
    SECTION("operator with empty version") {
        VersionRange r(">=");
        REQUIRE_FALSE(r.valid());
        REQUIRE_FALSE(r.parseError().empty());
    }
    SECTION("unknown operator token falls back to ANY and marks invalid") {
        VersionRange r("@@bad");
        REQUIRE_FALSE(r.valid());
        REQUIRE_FALSE(r.parseError().empty());
    }
    SECTION("valid range has no parseError") {
        VersionRange r(">=1.0.0");
        REQUIRE(r.valid());
        REQUIRE(r.parseError().empty());
    }
}

// ---------------------------------------------------------------------------
// VersionRange: 单操作符前缀
// ---------------------------------------------------------------------------
TEST_CASE("VersionRange single operator prefixes", "[version][range][op]") {
    SECTION(">= operator") {
        VersionRange r(">=1.0.0");
        REQUIRE(r.valid());
        REQUIRE(r.toString() == ">=1.0.0");
    }
    SECTION("<= operator") {
        VersionRange r("<=2.0.0");
        REQUIRE(r.valid());
        REQUIRE(r.toString() == "<=2.0.0");
    }
    SECTION("== operator") {
        VersionRange r("==1.5.0");
        REQUIRE(r.valid());
        REQUIRE(r.toString() == "==1.5.0");
    }
    SECTION("= operator (alias for ==)") {
        VersionRange r("=1.5.0");
        REQUIRE(r.valid());
        REQUIRE(r.toString() == "==1.5.0");
    }
    SECTION("> operator") {
        VersionRange r(">1.0.0");
        REQUIRE(r.valid());
        REQUIRE(r.toString() == ">1.0.0");
    }
    SECTION("< operator") {
        VersionRange r("<2.0.0");
        REQUIRE(r.valid());
        REQUIRE(r.toString() == "<2.0.0");
    }
    SECTION("bare version is EQUAL") {
        VersionRange r("1.2.3");
        REQUIRE(r.valid());
        REQUIRE(r.toString() == "==1.2.3");
    }
}

// ---------------------------------------------------------------------------
// Constraint::matches 边界
// ---------------------------------------------------------------------------
TEST_CASE("VersionRange Constraint matches boundary values", "[version][match]") {
    SECTION("LESS_EQUAL at boundary") {
        VersionRange r("<=1.0.0");
        std::vector<std::string> avail = {"1.0.0", "1.0.1"};
        auto matched = r.getVersionsInRange(avail);
        REQUIRE(matched.size() == 1);
        REQUIRE(matched[0] == "1.0.0");
    }
    SECTION("GREATER_EQUAL at boundary") {
        VersionRange r(">=1.0.0");
        std::vector<std::string> avail = {"0.9.9", "1.0.0"};
        auto matched = r.getVersionsInRange(avail);
        REQUIRE(matched.size() == 1);
        REQUIRE(matched[0] == "1.0.0");
    }
    SECTION("EQUAL matches only exact") {
        VersionRange r("==1.2.3");
        std::vector<std::string> avail = {"1.2.2", "1.2.3", "1.2.4"};
        auto matched = r.getVersionsInRange(avail);
        REQUIRE(matched.size() == 1);
        REQUIRE(matched[0] == "1.2.3");
    }
}

// ---------------------------------------------------------------------------
// ds-editor-lite 真实使用场景：声库版本比较
//
// Lite 在 SynthrtEngine::initialize 中按 packageId 收集 voicebank 路径并传给
// LanguageService。VersionRange 用于依赖解析时筛选候选版本。
// ---------------------------------------------------------------------------
TEST_CASE("VersionRange realistic voicebank version filtering",
          "[version][realworld]") {
    SECTION("filter voicebank versions >=1.0.0 <2.0.0") {
        VersionRange r(">=1.0.0 <2.0.0");
        std::vector<std::string> voicebanks = {
            "0.9.0",   // 旧版本
            "1.0.0",   // 边界
            "1.2.0",   // 主流
            "1.5.3",   // 最新 1.x
            "2.0.0",   // 新 major
            "2.1.0",
        };
        auto matched = r.getVersionsInRange(voicebanks);
        REQUIRE(matched.size() == 3);
        REQUIRE(matched[0] == "1.5.3"); // 降序
        REQUIRE(matched[1] == "1.2.0");
        REQUIRE(matched[2] == "1.0.0");
    }
}
