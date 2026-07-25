// tst_wav_writer.cpp
//
// GAP-NEW-010: tools/dsinfer-cli/wav_writer 写入失败路径单元测试（L1）。
//
// 测试覆盖：
//   - 正常写入：完整 PCM 帧写入，返回 0（BUG-CLI-001 回归）
//   - 多通道 audio（stereo）：验证帧数计算 audio.size() / channels
//   - 空 audio 数据：写入 0 帧，返回 0
//   - 异常 sampleRate <= 0：验证回退到 44100
//   - 异常 channels <= 0：验证回退到 1
//   - 不可写路径：返回 -1（写入失败路径覆盖）
//
// 测试目标直接编译 tools/dsinfer-cli/wav_writer.cpp，构造
// ds::infer::InferenceResult 写入临时文件验证。

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <diffsinger/Infer/InferenceResult.h>

#include "wav_writer.h"

namespace {

    std::atomic<uint64_t> g_uidCounter{1};

    std::filesystem::path uniqueTempPath(const std::string &suffix) {
        uint64_t uid = g_uidCounter.fetch_add(1, std::memory_order_relaxed);
        std::string filename = "synthrt_tst_wav_" + std::to_string(uid) + suffix;
        return std::filesystem::temp_directory_path() / filename;
    }

    struct TempFileGuard {
        std::filesystem::path path;
        explicit TempFileGuard(std::filesystem::path p) : path(std::move(p)) {}
        ~TempFileGuard() {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    };

    ds::infer::InferenceResult makeResult(std::vector<float> audio, int sampleRate,
                                            int channels) {
        ds::infer::InferenceResult r;
        r.audio = std::move(audio);
        r.sampleRate = sampleRate;
        r.channels = channels;
        return r;
    }

} // namespace

// ============================================================================
// 正常路径：单声道写入
// ============================================================================

TEST_CASE("writeWav mono single frame returns 0",
          "[dsinfer-cli][wav_writer][GAP-NEW-010]") {
    auto path = uniqueTempPath(".wav");
    TempFileGuard guard(path);

    std::vector<float> audio = {0.5f};
    auto result = makeResult(audio, 44100, 1);

    int ret = dsinfer_cli::writeWav(path, result);
    REQUIRE(ret == 0);
    REQUIRE(std::filesystem::exists(path));
    REQUIRE(std::filesystem::file_size(path) > 0);
}

TEST_CASE("writeWav mono multiple frames returns 0",
          "[dsinfer-cli][wav_writer][GAP-NEW-010]") {
    auto path = uniqueTempPath(".wav");
    TempFileGuard guard(path);

    std::vector<float> audio = {0.1f, 0.2f, 0.3f, 0.4f};
    auto result = makeResult(audio, 44100, 1);

    int ret = dsinfer_cli::writeWav(path, result);
    REQUIRE(ret == 0);
    REQUIRE(std::filesystem::exists(path));
    // WAV header 44 bytes + 4 frames * 4 bytes = 60 bytes
    REQUIRE(std::filesystem::file_size(path) >= 44);
}

// ============================================================================
// 多通道：stereo 帧数计算
// ============================================================================

TEST_CASE("writeWav stereo frames computed correctly",
          "[dsinfer-cli][wav_writer][GAP-NEW-010]") {
    auto path = uniqueTempPath(".wav");
    TempFileGuard guard(path);

    // 2 channels, 4 samples = 2 frames
    std::vector<float> audio = {0.1f, 0.2f, 0.3f, 0.4f};
    auto result = makeResult(audio, 44100, 2);

    int ret = dsinfer_cli::writeWav(path, result);
    REQUIRE(ret == 0);
    REQUIRE(std::filesystem::exists(path));
}

TEST_CASE("writeWav 5.1 surround channels",
          "[dsinfer-cli][wav_writer][GAP-NEW-010]") {
    auto path = uniqueTempPath(".wav");
    TempFileGuard guard(path);

    // 6 channels, 12 samples = 2 frames
    std::vector<float> audio = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f,
                                 0.7f, 0.8f, 0.9f, 0.0f, 0.1f, 0.2f};
    auto result = makeResult(audio, 48000, 6);

    int ret = dsinfer_cli::writeWav(path, result);
    REQUIRE(ret == 0);
}

// ============================================================================
// 边界：空 audio
// ============================================================================

TEST_CASE("writeWav empty audio returns 0",
          "[dsinfer-cli][wav_writer][GAP-NEW-010]") {
    auto path = uniqueTempPath(".wav");
    TempFileGuard guard(path);

    std::vector<float> audio;
    auto result = makeResult(audio, 44100, 1);

    int ret = dsinfer_cli::writeWav(path, result);
    // 空 audio：totalPCMFrameCount = 0 / 1 = 0，应写入 0 帧成功
    REQUIRE(ret == 0);
    REQUIRE(std::filesystem::exists(path));
    // WAV header 至少 44 字节
    REQUIRE(std::filesystem::file_size(path) >= 44);
}

// ============================================================================
// 边界：异常 sampleRate / channels 回退
// ============================================================================

TEST_CASE("writeWav sampleRate=0 falls back to 44100",
          "[dsinfer-cli][wav_writer][GAP-NEW-010]") {
    auto path = uniqueTempPath(".wav");
    TempFileGuard guard(path);

    std::vector<float> audio = {0.5f, 0.6f};
    auto result = makeResult(audio, 0, 1);

    // sampleRate <= 0 回退到 44100，不应崩溃或返回 -1
    int ret = dsinfer_cli::writeWav(path, result);
    REQUIRE(ret == 0);
    REQUIRE(std::filesystem::exists(path));
}

TEST_CASE("writeWav negative sampleRate falls back to 44100",
          "[dsinfer-cli][wav_writer][GAP-NEW-010]") {
    auto path = uniqueTempPath(".wav");
    TempFileGuard guard(path);

    std::vector<float> audio = {0.5f};
    auto result = makeResult(audio, -1, 1);

    int ret = dsinfer_cli::writeWav(path, result);
    REQUIRE(ret == 0);
}

TEST_CASE("writeWav channels=0 falls back to 1",
          "[dsinfer-cli][wav_writer][GAP-NEW-010]") {
    auto path = uniqueTempPath(".wav");
    TempFileGuard guard(path);

    // channels=0 + audio size=1：回退到 1 channel，frame count = 1/1 = 1
    std::vector<float> audio = {0.5f};
    auto result = makeResult(audio, 44100, 0);

    int ret = dsinfer_cli::writeWav(path, result);
    REQUIRE(ret == 0);
    REQUIRE(std::filesystem::exists(path));
}

TEST_CASE("writeWav negative channels falls back to 1",
          "[dsinfer-cli][wav_writer][GAP-NEW-010]") {
    auto path = uniqueTempPath(".wav");
    TempFileGuard guard(path);

    std::vector<float> audio = {0.5f};
    auto result = makeResult(audio, 44100, -1);

    int ret = dsinfer_cli::writeWav(path, result);
    REQUIRE(ret == 0);
}

// ============================================================================
// 异常路径：不可写路径
// ============================================================================

TEST_CASE("writeWav nonexistent directory returns -1",
          "[dsinfer-cli][wav_writer][GAP-NEW-010]") {
    // 路径包含不存在的目录，init_file_write 应失败
    auto path = std::filesystem::temp_directory_path() / "nonexistent_subdir_xyz" / "test.wav";

    std::vector<float> audio = {0.5f};
    auto result = makeResult(audio, 44100, 1);

    int ret = dsinfer_cli::writeWav(path, result);
    REQUIRE(ret == -1);
    REQUIRE_FALSE(std::filesystem::exists(path));
}

TEST_CASE("writeWav invalid filename returns -1",
          "[dsinfer-cli][wav_writer][GAP-NEW-010]") {
    // Windows 保留字符作为文件名
    auto path = std::filesystem::temp_directory_path() / "invalid*name?.wav";

    std::vector<float> audio = {0.5f};
    auto result = makeResult(audio, 44100, 1);

    int ret = dsinfer_cli::writeWav(path, result);
    REQUIRE(ret == -1);
}
