// tst_dsinfer_cli_internals.cpp
//
// GAP-NEW-004: tools/dsinfer-cli/segment_builder 边界用例单元测试（L1）。
//
// 测试覆盖：
//   - 空输入
//   - 单音符输入
//   - 完全连续音符（无 gap）
//   - 完全重叠音符（应被过滤）
//   - 超长单音符（maxDurationSec 边界）
//   - maxDurationSec == 0（每段一个音符）
//   - maxDurationSec 极小（导致每个音符独占一段）
//
// 测试目标通过直接编译 tools/dsinfer-cli/segment_builder.cpp 源文件
// 来访问内部纯逻辑函数（无需修改 dsinfer-cli 源码接口 D-11）。

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <vector>

// 直接 include 源文件，使其符号在本测试目标中可见。
// 注：segment_builder.cpp 仅依赖 STL，无外部库依赖。
// target_include_directories 已添加 tools/dsinfer-cli 到搜索路径。
#include "note_data.h"
#include "segment_builder.h"

namespace {

MidiNote makeNote(uint32_t startTick, uint32_t endTick, int key, double startMs, double endMs,
                  std::string lyric = "a") {
    MidiNote n;
    n.startTick = startTick;
    n.endTick = endTick;
    n.key = key;
    n.startMs = startMs;
    n.endMs = endMs;
    n.lyric = std::move(lyric);
    return n;
}

} // namespace

// ============================================================================
// buildContinuousPiece
// ============================================================================

TEST_CASE("buildContinuousPiece empty input returns empty piece",
          "[dsinfer-cli][segment_builder][GAP-NEW-004]") {
    std::vector<MidiNote> input;
    auto piece = buildContinuousPiece(input);
    REQUIRE(piece.notes.empty());
}

TEST_CASE("buildContinuousPiece single note preserved",
          "[dsinfer-cli][segment_builder][GAP-NEW-004]") {
    std::vector<MidiNote> input{makeNote(0, 100, 60, 0.0, 1000.0)};
    auto piece = buildContinuousPiece(input);
    REQUIRE(piece.notes.size() == 1);
    REQUIRE(piece.notes[0].startTick == 0);
    REQUIRE(piece.notes[0].endTick == 100);
}

TEST_CASE("buildContinuousPiece filters zero-duration notes",
          "[dsinfer-cli][segment_builder][GAP-NEW-004]") {
    // startTick == endTick 的音符应被过滤（endTick > startTick 检查）
    std::vector<MidiNote> input{
        makeNote(0, 0, 60, 0.0, 0.0),    // 零长度
        makeNote(0, 100, 60, 0.0, 1000.0)  // 正常
    };
    auto piece = buildContinuousPiece(input);
    REQUIRE(piece.notes.size() == 1);
    REQUIRE(piece.notes[0].endTick == 100);
}

TEST_CASE("buildContinuousPiece filters inverted notes",
          "[dsinfer-cli][segment_builder][GAP-NEW-004]") {
    // endTick < startTick 的音符应被过滤
    std::vector<MidiNote> input{
        makeNote(100, 50, 60, 1000.0, 500.0),  // 反向
        makeNote(0, 100, 60, 0.0, 1000.0)
    };
    auto piece = buildContinuousPiece(input);
    REQUIRE(piece.notes.size() == 1);
    REQUIRE(piece.notes[0].startTick == 0);
}

TEST_CASE("buildContinuousPiece filters overlapping notes",
          "[dsinfer-cli][segment_builder][GAP-NEW-004]") {
    // 重叠音符：第二个音符 startTick < lastEnd 应被过滤
    std::vector<MidiNote> input{
        makeNote(0, 200, 60, 0.0, 2000.0),
        makeNote(100, 300, 62, 1000.0, 3000.0),  // 重叠，应过滤
        makeNote(200, 400, 64, 2000.0, 4000.0)   // 不重叠，应保留
    };
    auto piece = buildContinuousPiece(input);
    REQUIRE(piece.notes.size() == 2);
    REQUIRE(piece.notes[0].startTick == 0);
    REQUIRE(piece.notes[1].startTick == 200);
}

TEST_CASE("buildContinuousPiece sorts notes by startTick then key",
          "[dsinfer-cli][segment_builder][GAP-NEW-004]") {
    // 乱序输入应被排序
    std::vector<MidiNote> input{
        makeNote(200, 400, 64, 2000.0, 4000.0),
        makeNote(0, 100, 60, 0.0, 1000.0),
        makeNote(100, 200, 62, 1000.0, 2000.0)
    };
    auto piece = buildContinuousPiece(input);
    REQUIRE(piece.notes.size() == 3);
    REQUIRE(piece.notes[0].startTick == 0);
    REQUIRE(piece.notes[1].startTick == 100);
    REQUIRE(piece.notes[2].startTick == 200);
}

// ============================================================================
// buildContinuousSegments
// ============================================================================

TEST_CASE("buildContinuousSegments empty input returns no segments",
          "[dsinfer-cli][segment_builder][GAP-NEW-004]") {
    std::vector<MidiNote> input;
    auto pieces = buildContinuousSegments(input, 10.0);
    REQUIRE(pieces.empty());
}

TEST_CASE("buildContinuousSegments single note single segment",
          "[dsinfer-cli][segment_builder][GAP-NEW-004]") {
    std::vector<MidiNote> input{makeNote(0, 100, 60, 0.0, 1000.0)};
    auto pieces = buildContinuousSegments(input, 10.0);
    REQUIRE(pieces.size() == 1);
    REQUIRE(pieces[0].notes.size() == 1);
}

TEST_CASE("buildContinuousSegments maxDurationSec zero yields_one_per_note",
          "[dsinfer-cli][segment_builder][GAP-NEW-004]") {
    // maxDurationSec == 0：第一个音符必进 buffer，第二个音符因
    // cur.endMs - segmentStartMs > 0 触发 break，每段一个音符。
    std::vector<MidiNote> input{
        makeNote(0, 100, 60, 0.0, 1000.0),
        makeNote(100, 200, 62, 1000.0, 2000.0),
        makeNote(200, 300, 64, 2000.0, 3000.0)
    };
    auto pieces = buildContinuousSegments(input, 0.0);
    REQUIRE(pieces.size() == 3);
    REQUIRE(pieces[0].notes.size() == 1);
    REQUIRE(pieces[1].notes.size() == 1);
    REQUIRE(pieces[2].notes.size() == 1);
}

TEST_CASE("buildContinuousSegments very small maxDurationSec",
          "[dsinfer-cli][segment_builder][GAP-NEW-004]") {
    // maxDurationSec 极小（0.001 秒）：每个音符独占一段
    std::vector<MidiNote> input{
        makeNote(0, 100, 60, 0.0, 1000.0),
        makeNote(100, 200, 62, 1000.0, 2000.0)
    };
    auto pieces = buildContinuousSegments(input, 0.001);
    REQUIRE(pieces.size() == 2);
}

TEST_CASE("buildContinuousSegments large maxDurationSec groups_all",
          "[dsinfer-cli][segment_builder][GAP-NEW-004]") {
    // maxDurationSec 足够大：所有音符分到同一段
    std::vector<MidiNote> input{
        makeNote(0, 100, 60, 0.0, 1000.0),
        makeNote(100, 200, 62, 1000.0, 2000.0),
        makeNote(200, 300, 64, 2000.0, 3000.0)
    };
    auto pieces = buildContinuousSegments(input, 100.0);
    REQUIRE(pieces.size() == 1);
    REQUIRE(pieces[0].notes.size() == 3);
}

TEST_CASE("buildContinuousSegments splits on maxDurationSec",
          "[dsinfer-cli][segment_builder][GAP-NEW-004]") {
    // 3 个音符，每个 1 秒；maxDurationSec=2.0 应分成 2 段（2+1）
    std::vector<MidiNote> input{
        makeNote(0, 100, 60, 0.0, 1000.0),
        makeNote(100, 200, 62, 1000.0, 2000.0),
        makeNote(200, 300, 64, 2000.0, 3000.0)
    };
    auto pieces = buildContinuousSegments(input, 2.0);
    // 第 1 段：notes[0] + notes[1]（notes[2] endMs=3000 > 0+2000 触发 break）
    // 第 2 段：notes[2]
    REQUIRE(pieces.size() == 2);
    REQUIRE(pieces[0].notes.size() == 2);
    REQUIRE(pieces[1].notes.size() == 1);
}

TEST_CASE("buildContinuousSegments filters overlapping before_splitting",
          "[dsinfer-cli][segment_builder][GAP-NEW-004]") {
    // 重叠音符应先被过滤，再分段
    std::vector<MidiNote> input{
        makeNote(0, 200, 60, 0.0, 2000.0),
        makeNote(100, 300, 62, 1000.0, 3000.0),  // 重叠，过滤
        makeNote(200, 400, 64, 2000.0, 4000.0)
    };
    auto pieces = buildContinuousSegments(input, 10.0);
    // 仅 2 个有效音符，分到同一段
    REQUIRE(pieces.size() == 1);
    REQUIRE(pieces[0].notes.size() == 2);
}
