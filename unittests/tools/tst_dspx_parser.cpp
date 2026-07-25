// tst_dspx_parser.cpp
//
// GAP-NEW-007: tools/dsinfer-cli/dspx_parser 边界用例单元测试（L1）。
//
// 测试覆盖：
//   - dspxTickToMs 空 tempos 列表（使用默认 BPM 120）
//   - BPM=0 → 抛 std::runtime_error（BUG-CLI-007 回归）
//   - BPM=负数 → 抛 std::runtime_error
//   - 单 tempo BPM=120 tick=0 → 0 ms
//   - 单 tempo BPM=120 tick=480 → 500 ms（1 quarter = 500 ms）
//   - 多 tempo 段切换
//   - 大 tick（>INT_MAX）验证 int64_t 不截断（BUG-CLI-008 回归）
//
// 测试目标通过 #include "dspx_parser.cpp" 单文件编译访问 static 函数
// dspxTickToMs。注：parseDspx 也会被编译进测试目标，需要链接
// opendspx::opendspxserializer（CMake 条件编译，与生产一致）。
//
// 构建条件：当且仅当 opendspx::opendspxserializer target 存在时
// （DSINFER_CLI_HAS_OPENDSPX 已定义），本测试目标才会被构建。

#ifdef DSINFER_CLI_HAS_OPENDSPX

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <vector>

using Catch::Approx;

// 直接 include 源文件，使 static 函数 dspxTickToMs 在测试中可见。
// dspx_parser.cpp 内部 include 了 opendspx/model.h 等，需要 opendspx
// 头文件路径可访问（target_link_libraries opendspx::opendspxserializer
// 会传递 include 路径）。
#include "dspx_parser.cpp"

namespace {

    opendspx::Tempo makeTempo(int64_t pos, double bpm) {
        opendspx::Tempo t;
        t.pos = pos;
        t.value = bpm;
        return t;
    }

} // namespace

// ============================================================================
// dspxTickToMs BPM 边界
// ============================================================================

TEST_CASE("dspxTickToMs empty tempos uses default BPM 120",
          "[dsinfer-cli][dspx_parser][GAP-NEW-007]") {
    std::vector<opendspx::Tempo> tempos;
    // 空 tempos：实现会 push_back 一个默认 Tempo{pos=0, value=120}
    double ms = dspxTickToMs(480, tempos);
    // 480 ticks = 1 quarter = 60000 / 120 = 500 ms
    REQUIRE(ms == Approx(500.0).epsilon(0.001));
}

TEST_CASE("dspxTickToMs zero BPM throws runtime_error",
          "[dsinfer-cli][dspx_parser][GAP-NEW-007]") {
    std::vector<opendspx::Tempo> tempos{makeTempo(0, 0.0)};
    REQUIRE_THROWS_AS(dspxTickToMs(480, tempos), std::runtime_error);
}

TEST_CASE("dspxTickToMs negative BPM throws runtime_error",
          "[dsinfer-cli][dspx_parser][GAP-NEW-007]") {
    std::vector<opendspx::Tempo> tempos{makeTempo(0, -60.0)};
    REQUIRE_THROWS_AS(dspxTickToMs(480, tempos), std::runtime_error);
}

TEST_CASE("dspxTickToMs zero BPM in middle tempo throws",
          "[dsinfer-cli][dspx_parser][GAP-NEW-007]") {
    // 第一个 tempo 正常，第二个 tempo BPM=0，应抛
    std::vector<opendspx::Tempo> tempos{
        makeTempo(0, 120.0),
        makeTempo(480, 0.0)
    };
    REQUIRE_THROWS_AS(dspxTickToMs(960, tempos), std::runtime_error);
}

// ============================================================================
// dspxTickToMs 正常路径
// ============================================================================

TEST_CASE("dspxTickToMs tick=0 returns 0 ms",
          "[dsinfer-cli][dspx_parser][GAP-NEW-007]") {
    std::vector<opendspx::Tempo> tempos{makeTempo(0, 120.0)};
    double ms = dspxTickToMs(0, tempos);
    REQUIRE(ms == Approx(0.0).epsilon(0.001));
}

TEST_CASE("dspxTickToMs 480 ticks at BPM=120 equals 500 ms",
          "[dsinfer-cli][dspx_parser][GAP-NEW-007]") {
    // 480 ticks = 1 quarter；BPM=120 → 1 quarter = 60000/120 = 500 ms
    std::vector<opendspx::Tempo> tempos{makeTempo(0, 120.0)};
    double ms = dspxTickToMs(480, tempos);
    REQUIRE(ms == Approx(500.0).epsilon(0.001));
}

TEST_CASE("dspxTickToMs 960 ticks at BPM=120 equals 1000 ms",
          "[dsinfer-cli][dspx_parser][GAP-NEW-007]") {
    std::vector<opendspx::Tempo> tempos{makeTempo(0, 120.0)};
    double ms = dspxTickToMs(960, tempos);
    REQUIRE(ms == Approx(1000.0).epsilon(0.001));
}

TEST_CASE("dspxTickToMs 480 ticks at BPM=60 equals 1000 ms",
          "[dsinfer-cli][dspx_parser][GAP-NEW-007]") {
    // BPM=60 → 1 quarter = 60000/60 = 1000 ms
    std::vector<opendspx::Tempo> tempos{makeTempo(0, 60.0)};
    double ms = dspxTickToMs(480, tempos);
    REQUIRE(ms == Approx(1000.0).epsilon(0.001));
}

// ============================================================================
// dspxTickToMs 多 tempo 段切换
// ============================================================================

TEST_CASE("dspxTickToMs multi-tempo segment switch",
          "[dsinfer-cli][dspx_parser][GAP-NEW-007]") {
    // tick 0-480: BPM=120 (500 ms/quarter)
    // tick 480-960: BPM=60 (1000 ms/quarter)
    // 总 tick=960 → 500 + 1000 = 1500 ms
    std::vector<opendspx::Tempo> tempos{
        makeTempo(0, 120.0),
        makeTempo(480, 60.0)
    };
    double ms = dspxTickToMs(960, tempos);
    REQUIRE(ms == Approx(1500.0).epsilon(0.001));
}

TEST_CASE("dspxTickToMs unsorted tempos are sorted internally",
          "[dsinfer-cli][dspx_parser][GAP-NEW-007]") {
    // tempos 乱序输入，dspxTickToMs 内部应排序
    std::vector<opendspx::Tempo> tempos{
        makeTempo(480, 60.0),
        makeTempo(0, 120.0)
    };
    double ms = dspxTickToMs(960, tempos);
    REQUIRE(ms == Approx(1500.0).epsilon(0.001));
}

// ============================================================================
// BUG-CLI-008 回归：tick 累加超过 INT_MAX 验证 int64_t 不截断
// ============================================================================

TEST_CASE("dspxTickToMs large tick beyond INT_MAX no truncation",
          "[dsinfer-cli][dspx_parser][GAP-NEW-007]") {
    // INT_MAX ≈ 21 亿；tick = 30 亿（超过 INT_MAX）
    // 验证 int64_t 不截断。BPM=120 → ms = tick * 60000 / 120 / 480
    //                                            = tick * 1.041666...
    // 对于 tick=3,000,000,000: ms ≈ 3,125,000,000
    std::vector<opendspx::Tempo> tempos{makeTempo(0, 120.0)};
    int64_t largeTick = 3000000000LL; // 30 亿
    double ms = dspxTickToMs(largeTick, tempos);
    // 期望 ≈ 3,125,000,000 ms（如果 int64_t 正确使用，不会有截断）
    REQUIRE(ms > 3.0e9);
    REQUIRE(ms < 3.2e9);
}

TEST_CASE("dspxTickToMs tempo.pos beyond INT_MAX",
          "[dsinfer-cli][dspx_parser][GAP-NEW-007]") {
    // tempo.pos 也用 int64_t，验证大 pos 不截断
    std::vector<opendspx::Tempo> tempos{
        makeTempo(0, 120.0),
        makeTempo(2000000000LL, 60.0)  // 20 亿 pos
    };
    // tick=4,000,000,000 应正确计算跨越两个 tempo
    REQUIRE_NOTHROW(dspxTickToMs(4000000000LL, tempos));
}

TEST_CASE("dspxTickToMs tempo.pos before cursor is skipped",
          "[dsinfer-cli][dspx_parser][GAP-NEW-007]") {
    // tempo.pos <= cursor 时直接更新 BPM，不累加 ms
    std::vector<opendspx::Tempo> tempos{
        makeTempo(0, 120.0),
        makeTempo(0, 60.0)  // 同 pos，应跳过累加但更新 BPM
    };
    double ms = dspxTickToMs(480, tempos);
    // 后一个 tempo 覆盖前一个，BPM=60 → 480 ticks = 1000 ms
    REQUIRE(ms == Approx(1000.0).epsilon(0.001));
}

TEST_CASE("dspxTickToMs tempo.pos beyond tick is ignored",
          "[dsinfer-cli][dspx_parser][GAP-NEW-007]") {
    // tempo.pos >= tick 时 break，不影响计算
    std::vector<opendspx::Tempo> tempos{
        makeTempo(0, 120.0),
        makeTempo(960, 60.0)  // pos=960 > tick=480，应被忽略
    };
    double ms = dspxTickToMs(480, tempos);
    REQUIRE(ms == Approx(500.0).epsilon(0.001));
}

#endif // DSINFER_CLI_HAS_OPENDSPX
