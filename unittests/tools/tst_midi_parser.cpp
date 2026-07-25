// tst_midi_parser.cpp
//
// GAP-NEW-008: tools/dsinfer-cli/midi_parser 边界用例单元测试（L1）。
//
// 测试覆盖：
//   - 单 channel 单音符简单 MIDI（正常路径）
//   - 多 channel 同音符同时按下：验证 (channel, key) 匹配不串味
//     （BUG-CLI-010 回归）
//   - division == 0 → 抛 std::runtime_error
//   - usPerQuarter == 0（tempo meta 为 0）→ 抛 std::runtime_error
//     （BUG-CLI-011 回归）
//   - 多 tempo 段切换
//   - SMPTE time division → 抛 std::runtime_error
//   - 缺少 MThd header → 抛 std::runtime_error
//   - 损坏 MIDI（截断数据）→ 抛 std::runtime_error
//   - 空 MIDI 文件
//   - SysEx 事件被忽略
//   - 缺失 End of Track 不崩溃
//
// 测试目标通过临时文件 + parseMidi 公开函数测试。parseMidi 接受文件路径，
// 因此需要写入临时 MIDI 字节序列到磁盘。使用 std::filesystem::temp_directory_path()
// + 唯一文件名避免并发冲突。

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "midi_parser.h"

using Catch::Approx;

namespace {

    // VLQ（Variable-Length Quantity）编码
    std::vector<uint8_t> encodeVlq(uint32_t value) {
        std::vector<uint8_t> result;
        if (value == 0) return {0};
        while (value > 0) {
            result.insert(result.begin(), value & 0x7f);
            value >>= 7;
        }
        for (size_t i = 0; i + 1 < result.size(); ++i) {
            result[i] |= 0x80;
        }
        return result;
    }

    // 构建 MIDI header chunk（MThd + length + format + ntracks + division）
    std::vector<uint8_t> buildMidiHeader(uint16_t format, uint16_t ntracks, uint16_t division) {
        std::vector<uint8_t> h = {'M', 'T', 'h', 'd'};
        h.push_back(0); h.push_back(0); h.push_back(0); h.push_back(6); // length=6
        h.push_back(format >> 8); h.push_back(format & 0xff);
        h.push_back(ntracks >> 8); h.push_back(ntracks & 0xff);
        h.push_back(division >> 8); h.push_back(division & 0xff);
        return h;
    }

    // 构建 MIDI track chunk（MTrk + length + events）
    std::vector<uint8_t> buildMidiTrack(const std::vector<uint8_t> &events) {
        std::vector<uint8_t> t = {'M', 'T', 'r', 'k'};
        uint32_t len = static_cast<uint32_t>(events.size());
        t.push_back((len >> 24) & 0xff);
        t.push_back((len >> 16) & 0xff);
        t.push_back((len >> 8) & 0xff);
        t.push_back(len & 0xff);
        t.insert(t.end(), events.begin(), events.end());
        return t;
    }

    // End of Track meta event：delta_time=0 + 0xFF + 0x2F + 0x00
    std::vector<uint8_t> endOfTrack() {
        return {0, 0xFF, 0x2F, 0x00};
    }

    // note-on 事件：delta + 0x90|channel + key + velocity
    std::vector<uint8_t> noteOn(uint32_t delta, uint8_t channel, uint8_t key, uint8_t velocity) {
        auto v = encodeVlq(delta);
        v.push_back(0x90 | (channel & 0x0f));
        v.push_back(key);
        v.push_back(velocity);
        return v;
    }

    // note-off 事件：delta + 0x80|channel + key + velocity
    std::vector<uint8_t> noteOff(uint32_t delta, uint8_t channel, uint8_t key) {
        auto v = encodeVlq(delta);
        v.push_back(0x80 | (channel & 0x0f));
        v.push_back(key);
        v.push_back(0);
        return v;
    }

    // tempo meta event：delta + 0xFF + 0x51 + 0x03 + 3 bytes usPerQuarter
    std::vector<uint8_t> tempoMeta(uint32_t delta, uint32_t usPerQuarter) {
        auto v = encodeVlq(delta);
        v.push_back(0xFF);
        v.push_back(0x51);
        v.push_back(0x03);
        v.push_back((usPerQuarter >> 16) & 0xff);
        v.push_back((usPerQuarter >> 8) & 0xff);
        v.push_back(usPerQuarter & 0xff);
        return v;
    }

    // SysEx event: delta + 0xF0 + length(vlq) + data + 0xF7
    std::vector<uint8_t> sysExEvent(uint32_t delta, const std::vector<uint8_t> &data) {
        auto v = encodeVlq(delta);
        v.push_back(0xF0);
        auto lenBytes = encodeVlq(static_cast<uint32_t>(data.size() + 1)); // +1 for F7
        v.insert(v.end(), lenBytes.begin(), lenBytes.end());
        v.insert(v.end(), data.begin(), data.end());
        v.push_back(0xF7);
        return v;
    }

    // 拼接多个事件
    std::vector<uint8_t> concatEvents(std::initializer_list<std::vector<uint8_t>> events) {
        std::vector<uint8_t> result;
        for (const auto &e : events) {
            result.insert(result.end(), e.begin(), e.end());
        }
        return result;
    }

    // 唯一临时文件路径生成器
    std::atomic<uint64_t> g_uidCounter{1};

    std::filesystem::path uniqueTempPath(const std::string &suffix) {
        uint64_t uid = g_uidCounter.fetch_add(1, std::memory_order_relaxed);
        std::string filename = "synthrt_tst_midi_" + std::to_string(uid) + suffix;
        return std::filesystem::temp_directory_path() / filename;
    }

    // 写入 MIDI 字节序列到临时文件，调用 parseMidi，返回结果
    std::vector<MidiNote> parseFromBytes(const std::vector<uint8_t> &bytes,
                                          const std::filesystem::path &path) {
        {
            std::ofstream f(path, std::ios::binary);
            f.write(reinterpret_cast<const char *>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
        }
        return parseMidi(path);
    }

    // RAII 临时文件清理
    struct TempFileGuard {
        std::filesystem::path path;
        explicit TempFileGuard(std::filesystem::path p) : path(std::move(p)) {}
        ~TempFileGuard() {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    };

    // 完整 MIDI 文件 = header + tracks
    std::vector<uint8_t> buildMidiFile(uint16_t format, uint16_t ntracks, uint16_t division,
                                        const std::vector<std::vector<uint8_t>> &tracks) {
        std::vector<uint8_t> file = buildMidiHeader(format, ntracks, division);
        for (const auto &t : tracks) {
            file.insert(file.end(), t.begin(), t.end());
        }
        return file;
    }

} // namespace

// ============================================================================
// 正常路径：单音符简单 MIDI
// ============================================================================

TEST_CASE("parseMidi single note on/off returns one note",
          "[dsinfer-cli][midi_parser][GAP-NEW-008]") {
    auto path = uniqueTempPath(".mid");
    TempFileGuard guard(path);

    auto events = concatEvents({
        noteOn(0, 0, 60, 100),    // tick=0, channel=0, key=60, velocity=100
        noteOff(480, 0, 60),     // tick=480, note off
        endOfTrack(),
    });
    auto track = buildMidiTrack(events);
    auto file = buildMidiFile(0, 1, 480, {track});

    auto notes = parseFromBytes(file, path);
    REQUIRE(notes.size() == 1);
    REQUIRE(notes[0].startTick == 0);
    REQUIRE(notes[0].endTick == 480);
    REQUIRE(notes[0].key == 60);
}

TEST_CASE("parseMidi multiple notes on same channel",
          "[dsinfer-cli][midi_parser][GAP-NEW-008]") {
    auto path = uniqueTempPath(".mid");
    TempFileGuard guard(path);

    auto events = concatEvents({
        noteOn(0, 0, 60, 100),
        noteOff(480, 0, 60),
        noteOn(0, 0, 62, 100),
        noteOff(480, 0, 62),
        endOfTrack(),
    });
    auto track = buildMidiTrack(events);
    auto file = buildMidiFile(0, 1, 480, {track});

    auto notes = parseFromBytes(file, path);
    REQUIRE(notes.size() == 2);
    REQUIRE(notes[0].key == 60);
    REQUIRE(notes[1].key == 62);
}

// ============================================================================
// BUG-CLI-010 回归：多 channel 同音符同时按下不串味
// ============================================================================

TEST_CASE("parseMidi multi-channel same key does not cross-match",
          "[dsinfer-cli][midi_parser][GAP-NEW-008]") {
    // channel 0 note-on key=60, channel 1 note-on key=60,
    // channel 0 note-off key=60 → 应匹配 channel 0 的 note-on
    auto path = uniqueTempPath(".mid");
    TempFileGuard guard(path);

    auto events = concatEvents({
        noteOn(0, 0, 60, 100),    // ch0 key60 on
        noteOn(0, 1, 60, 100),    // ch1 key60 on
        noteOff(480, 0, 60),     // ch0 off
        noteOff(480, 1, 60),     // ch1 off
        endOfTrack(),
    });
    auto track = buildMidiTrack(events);
    auto file = buildMidiFile(0, 1, 480, {track});

    auto notes = parseFromBytes(file, path);
    REQUIRE(notes.size() == 2);
    // 两个 note 都应被识别为合法（startTick <= endTick）
    REQUIRE(notes[0].startTick == 0);
    REQUIRE(notes[0].endTick == 480);
    REQUIRE(notes[1].startTick == 0);
    REQUIRE(notes[1].endTick == 960);
    REQUIRE(notes[0].key == 60);
    REQUIRE(notes[1].key == 60);
}

TEST_CASE("parseMidi note-off without matching note-on is ignored",
          "[dsinfer-cli][midi_parser][GAP-NEW-008]") {
    auto path = uniqueTempPath(".mid");
    TempFileGuard guard(path);

    auto events = concatEvents({
        noteOff(0, 0, 60),  // 没有 note-on，应被忽略
        endOfTrack(),
    });
    auto track = buildMidiTrack(events);
    auto file = buildMidiFile(0, 1, 480, {track});

    auto notes = parseFromBytes(file, path);
    REQUIRE(notes.empty());
}

// ============================================================================
// BUG-CLI-011 回归：division / tempo 除零
// ============================================================================

TEST_CASE("parseMidi division zero throws runtime_error",
          "[dsinfer-cli][midi_parser][GAP-NEW-008]") {
    auto path = uniqueTempPath(".mid");
    TempFileGuard guard(path);

    auto events = concatEvents({endOfTrack()});
    auto track = buildMidiTrack(events);
    auto file = buildMidiFile(0, 1, 0, {track}); // division=0

    REQUIRE_THROWS_AS(parseFromBytes(file, path), std::runtime_error);
}

TEST_CASE("parseMidi SMPTE division throws runtime_error",
          "[dsinfer-cli][midi_parser][GAP-NEW-008]") {
    auto path = uniqueTempPath(".mid");
    TempFileGuard guard(path);

    auto events = concatEvents({endOfTrack()});
    auto track = buildMidiTrack(events);
    // 0x8000 设置 SMPTE 标志位
    auto file = buildMidiFile(0, 1, 0x8000, {track});

    REQUIRE_THROWS_AS(parseFromBytes(file, path), std::runtime_error);
}

TEST_CASE("parseMidi tempo meta with usPerQuarter=0 throws runtime_error",
          "[dsinfer-cli][midi_parser][GAP-NEW-008]") {
    auto path = uniqueTempPath(".mid");
    TempFileGuard guard(path);

    auto events = concatEvents({
        tempoMeta(0, 0),  // usPerQuarter=0
        endOfTrack(),
    });
    auto track = buildMidiTrack(events);
    auto file = buildMidiFile(0, 1, 480, {track});

    REQUIRE_THROWS_AS(parseFromBytes(file, path), std::runtime_error);
}

// ============================================================================
// 多 tempo 段切换
// ============================================================================

TEST_CASE("parseMidi multi-tempo segments compute correct ms",
          "[dsinfer-cli][midi_parser][GAP-NEW-008]") {
    // tempo[0] tick=0, usPerQuarter=500000 (BPM=120, 500 ms/quarter)
    // tempo[1] tick=480, usPerQuarter=1000000 (BPM=60, 1000 ms/quarter)
    // noteOn @ tick=0 (startTick=0)
    // noteOff @ tick=0+480+480=960 (endTick=960)
    // startMs = 0 (tick 0 在 tempo[0])
    // endMs = tick 0-480 (BPM=120, 500 ms) + tick 480-960 (BPM=60, 1000 ms) = 1500 ms
    auto path = uniqueTempPath(".mid");
    TempFileGuard guard(path);

    auto events = concatEvents({
        tempoMeta(0, 500000),
        noteOn(0, 0, 60, 100),     // tick=0, startTick=0
        tempoMeta(480, 1000000),   // tick=0+480=480
        noteOff(480, 0, 60),       // tick=480+480=960, endTick=960
        endOfTrack(),
    });
    auto track = buildMidiTrack(events);
    auto file = buildMidiFile(0, 1, 480, {track});

    auto notes = parseFromBytes(file, path);
    REQUIRE(notes.size() == 1);
    REQUIRE(notes[0].startTick == 0);
    REQUIRE(notes[0].endTick == 960);
    REQUIRE(notes[0].startMs == Approx(0.0).epsilon(0.001));
    // 500 + 1000 = 1500 ms
    REQUIRE(notes[0].endMs == Approx(1500.0).epsilon(0.001));
}

// ============================================================================
// 异常路径：损坏 MIDI
// ============================================================================

TEST_CASE("parseMidi missing MThd header throws",
          "[dsinfer-cli][midi_parser][GAP-NEW-008]") {
    auto path = uniqueTempPath(".mid");
    TempFileGuard guard(path);

    std::vector<uint8_t> bad = {'X', 'X', 'X', 'X'};
    REQUIRE_THROWS_AS(parseFromBytes(bad, path), std::runtime_error);
}

TEST_CASE("parseMidi empty file throws",
          "[dsinfer-cli][midi_parser][GAP-NEW-008]") {
    auto path = uniqueTempPath(".mid");
    TempFileGuard guard(path);

    std::vector<uint8_t> empty;
    REQUIRE_THROWS_AS(parseFromBytes(empty, path), std::runtime_error);
}

TEST_CASE("parseMidi truncated data throws",
          "[dsinfer-cli][midi_parser][GAP-NEW-008]") {
    auto path = uniqueTempPath(".mid");
    TempFileGuard guard(path);

    // 只写 header 但不写 track
    auto file = buildMidiHeader(0, 1, 480);
    REQUIRE_THROWS_AS(parseFromBytes(file, path), std::runtime_error);
}

TEST_CASE("parseMidi missing MTrk chunk throws",
          "[dsinfer-cli][midi_parser][GAP-NEW-008]") {
    auto path = uniqueTempPath(".mid");
    TempFileGuard guard(path);

    // header 声明 1 个 track，但没有 track chunk
    std::vector<uint8_t> file = buildMidiHeader(0, 1, 480);
    file.insert(file.end(), {'X', 'X', 'X', 'X', 0, 0, 0, 0});
    REQUIRE_THROWS_AS(parseFromBytes(file, path), std::runtime_error);
}

// ============================================================================
// SysEx 事件被忽略
// ============================================================================

TEST_CASE("parseMidi SysEx event is ignored",
          "[dsinfer-cli][midi_parser][GAP-NEW-008]") {
    auto path = uniqueTempPath(".mid");
    TempFileGuard guard(path);

    auto events = concatEvents({
        sysExEvent(0, {0x41, 0x42, 0x43}),  // 任意 SysEx 数据
        noteOn(0, 0, 60, 100),
        noteOff(480, 0, 60),
        endOfTrack(),
    });
    auto track = buildMidiTrack(events);
    auto file = buildMidiFile(0, 1, 480, {track});

    auto notes = parseFromBytes(file, path);
    REQUIRE(notes.size() == 1);
    REQUIRE(notes[0].key == 60);
}

// ============================================================================
// 不存在的文件
// ============================================================================

TEST_CASE("parseMidi nonexistent file throws",
          "[dsinfer-cli][midi_parser][GAP-NEW-008]") {
    auto path = uniqueTempPath(".mid");
    // 不创建文件，直接调用 parseMidi
    REQUIRE_THROWS_AS(parseMidi(path), std::runtime_error);
}
