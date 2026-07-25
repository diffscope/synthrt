// lib/Audio edge condition test cases (AUD-001 ~ AUD-013)
//
// Covers behavior of srt::audio::IAudioDecoder (FfmpegAudioDecoder implementation)
// and AudioBuffer under abnormal input, out-of-bounds access, and concurrency
// scenarios. See task description for test matrix; corresponds to the
// implementation in lib/Audio/src/FfmpegAudioDecoder.cpp and lib/Audio/src/AudioBuffer.cpp.
//
// === API difference notes vs. test matrix ===
// The following cases have actual return values that differ from the matrix
// description (adjusted per the actual API):
//   - AUD-001 (P0): Matrix expected Error(InvalidArg). Actual probe("") fails
//     via avformat_open_input and returns ErrorCode::AudioDecodeFailed
//     (lib/Audio/src/FfmpegAudioDecoder.cpp:213); no dedicated InvalidArgument branch.
//   - AUD-002 (P0): Matrix expected Error(FileNotFound). Actual probe("/nonexistent.wav")
//     also returns ErrorCode::AudioDecodeFailed (avformat_open_input unified error).
//   - AUD-003 (P0): Matrix expected Error(UnsupportedFormat). Actual open() on a text
//     file returns ErrorCode::AudioDecodeFailed (avformat_open_input cannot recognize
//     format) or AudioUnsupportedFormat (opens but no audio stream). Both are acceptable.
//   - AUD-004 (P0): Matrix expected Error(NotInitialized) or empty buffer. Actual read()
//     when not open returns ErrorCode::AudioDecodeFailed ("read: no file is open").
//   - AUD-005 (P0): read(0) returns empty buffer (matches matrix). read(-1) passes an
//     oversized value to data.reserve(static_cast<size_t>(-1 * channels * bps)) and
//     throws std::length_error/bad_alloc instead of returning Error (API difference).
//   - AUD-010 (P0): slice() guards out-of-bounds startFrame with assert. Release builds
//     (NDEBUG) take defensive clamping and return empty buffer; Debug builds abort the
//     process via assert. Test verifies empty buffer return under NDEBUG, otherwise SKIP.
//   - AUD-011 (P1): sampleAt() guards out-of-bounds channel with assert. Matrix allows
//     "return 0 or assert". Debug aborts; Release reads out-of-bounds (UB). Use SKIP to
//     document, and verify valid channel access as a baseline.
//   - AUD-012 (P1): floats() guards format mismatch with assert. Matrix allows "return
//     empty span or assert". Debug aborts; Release reinterprets with wrong format (UB).
//     Use SKIP to document, and verify valid Float32 access as a baseline.
//   - AUD-013 (P2): view mode source data release is UB, marked with SKIP().

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <synthrt/Audio/AudioBuffer.h>
#include <synthrt/Audio/AudioFormatInfo.h>
#include <synthrt/Audio/FfmpegAudioDecoder.h>
#include <synthrt/Audio/IAudioDecoder.h>
#include <synthrt/Audio/SampleFormat.h>
#include <synthrt/Core/Support/Diagnostic.h>
#include <synthrt/Core/Support/Error.h>
#include <synthrt/Core/Support/Expected.h>

using namespace srt::audio;
using srt::core::Error;
using srt::core::ErrorCode;
using srt::core::Expected;

namespace srt::core {
// Catch2 needs operator<< to stream ErrorCode values in failed assertions.
inline std::ostream &operator<<(std::ostream &os, const ErrorCode &code) {
    return os << errorCodeToString(code);
}
} // namespace srt::core

namespace {

    std::atomic<uint64_t> g_uidCounter{1};

    // Generate a unique temporary file path.
    std::filesystem::path uniqueTempPath(const std::string &suffix) {
        uint64_t uid = g_uidCounter.fetch_add(1, std::memory_order_relaxed);
        return std::filesystem::temp_directory_path() /
               ("srt-audio-edge-" + std::to_string(uid) + suffix);
    }

    // RAII temporary file guard; deletes the file on destruction.
    struct TempFileGuard {
        std::filesystem::path path;
        explicit TempFileGuard(std::filesystem::path p) : path(std::move(p)) {}
        ~TempFileGuard() {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    };

    // Write a minimal valid PCM WAV file (Int16, silent).
    // Used by AUD-005/006/007 tests that require an "open" state.
    void writeMinimalWav(const std::filesystem::path &path, int sampleRate,
                         int channels, int64_t numFrames) {
        const int bitsPerSample = 16;
        const int bytesPerSample = bitsPerSample / 8;
        const int blockAlign = channels * bytesPerSample;
        const int byteRate = sampleRate * blockAlign;
        const int64_t dataSize = numFrames * blockAlign;

        std::ofstream f(path, std::ios::binary);
        REQUIRE(f.is_open());

        auto writeVal = [&](const auto &val) {
            f.write(reinterpret_cast<const char *>(&val), sizeof(val));
        };

        // RIFF header
        f.write("RIFF", 4);
        int32_t chunkSize = static_cast<int32_t>(36 + dataSize);
        writeVal(chunkSize);
        f.write("WAVE", 4);

        // fmt chunk
        f.write("fmt ", 4);
        int32_t fmtSize = 16;
        writeVal(fmtSize);
        int16_t audioFormat = 1; // PCM
        writeVal(audioFormat);
        int16_t numCh = static_cast<int16_t>(channels);
        writeVal(numCh);
        int32_t sr = sampleRate;
        writeVal(sr);
        int32_t br = byteRate;
        writeVal(br);
        int16_t ba = static_cast<int16_t>(blockAlign);
        writeVal(ba);
        int16_t bps = static_cast<int16_t>(bitsPerSample);
        writeVal(bps);

        // data chunk
        f.write("data", 4);
        int32_t ds = static_cast<int32_t>(dataSize);
        writeVal(ds);

        // Silent data (all zeros)
        std::vector<char> zeros(static_cast<size_t>(dataSize), 0);
        f.write(zeros.data(), static_cast<std::streamsize>(zeros.size()));
    }

} // namespace

// ---------------------------------------------------------------------------
// AUD-001/002: probe rejects invalid paths
// Merged: both cases share the FfmpegAudioDecoder construction and verify
// the same error code (AudioDecodeFailed). SECTION form keeps the per-input
// traceability while halving the decoder setup cost.
// ---------------------------------------------------------------------------
TEST_CASE("AUD-001/002: probe rejects invalid paths", "[audio][edge]") {
    FfmpegAudioDecoder decoder;

    SECTION("AUD-001: empty path -> AudioDecodeFailed") {
        // API difference: matrix expected InvalidArgument; actual returns
        // AudioDecodeFailed (avformat_open_input unified error path).
        auto result = decoder.probe("");
        REQUIRE_FALSE(result.hasValue());
        REQUIRE(result.error().code() == ErrorCode::AudioDecodeFailed);
    }

    SECTION("AUD-002: nonexistent file -> AudioDecodeFailed") {
        // API difference: matrix expected FileNotFound; actual returns
        // AudioDecodeFailed (avformat_open_input unified error path).
        auto result = decoder.probe("/nonexistent/path/does-not-exist.wav");
        REQUIRE_FALSE(result.hasValue());
        REQUIRE(result.error().code() == ErrorCode::AudioDecodeFailed);
    }

    SECTION("AUD-002b: directory path -> AudioDecodeFailed") {
        // Boundary: probing a directory (not a file) must also be rejected
        // with AudioDecodeFailed, not crash.
        auto result = decoder.probe(std::filesystem::temp_directory_path().string());
        REQUIRE_FALSE(result.hasValue());
        REQUIRE(result.error().code() == ErrorCode::AudioDecodeFailed);
    }
}

// ---------------------------------------------------------------------------
// AUD-003: open with a non-audio file
// avformat_open_input cannot recognize the format of a text file and returns
// AudioDecodeFailed.
// ---------------------------------------------------------------------------
TEST_CASE("AUD-003: open rejects non-audio file", "[audio][edge]") {
    // Create a temporary text file (non-audio)
    auto txtPath = uniqueTempPath(".txt");
    TempFileGuard guard(txtPath);
    {
        std::ofstream f(txtPath, std::ios::binary);
        f << "this is not an audio file, just plain text content";
    }

    FfmpegAudioDecoder decoder;
    auto result = decoder.open(txtPath.string());
    REQUIRE_FALSE(result.hasValue());
    // avformat cannot recognize the format and returns AudioDecodeFailed, or
    // opens but finds no audio stream and returns AudioUnsupportedFormat.
    // Both are valid rejections.
    const auto code = result.error().code();
    REQUIRE((code == ErrorCode::AudioDecodeFailed ||
             code == ErrorCode::AudioUnsupportedFormat));
}

// ---------------------------------------------------------------------------
// AUD-004: read called before open
// API difference: actually returns AudioDecodeFailed (not NotInitialized)
// ---------------------------------------------------------------------------
TEST_CASE("AUD-004: read before open returns error", "[audio][edge]") {
    FfmpegAudioDecoder decoder;
    auto result = decoder.read(1024);
    REQUIRE_FALSE(result.hasValue());
    REQUIRE(result.error().code() == ErrorCode::AudioDecodeFailed);
}

// ---------------------------------------------------------------------------
// AUD-005: read with 0 or negative frameCount
// read(0) returns empty buffer (matches matrix). read(-1) throws due to an
// oversized reserve (API difference).
// ---------------------------------------------------------------------------
TEST_CASE("AUD-005: read with zero or negative frameCount", "[audio][edge]") {
    // Create a 1-second 8000Hz mono Int16 silent WAV
    auto wavPath = uniqueTempPath(".wav");
    TempFileGuard guard(wavPath);
    writeMinimalWav(wavPath, 8000, 1, 8000);

    FfmpegAudioDecoder decoder;
    REQUIRE(decoder.open(wavPath.string()).hasValue());

    SECTION("read(0) returns empty buffer") {
        auto result = decoder.read(0);
        REQUIRE(result.hasValue());
        REQUIRE(result->empty());
    }

    SECTION("read(-1) returns empty buffer (clamped to 0)") {
        // The implementation clamps frameCount <= 0 to 0 frames via an early
        // guard in read(), returning an empty buffer. This matches the matrix
        // expectation of "empty buffer or Error" (ROBUST-01) and avoids the
        // oversized-reserve throw path that would violate the no-exceptions-
        // across-module-boundaries rule (ROBUST-02 / CODING-02).
        auto result = decoder.read(-1);
        REQUIRE(result.hasValue());
        REQUIRE(result->empty());
    }
}

// ---------------------------------------------------------------------------
// AUD-006/007: seekToTime edge cases (negative, beyond duration, boundary)
// Merged: both cases share the writeMinimalWav + open setup (~16KB file I/O),
// so collapsing into one TEST_CASE with SECTIONs halves the decoder open cost.
// Adds boundary coverage for seekToTime(0.0) and seekToTime(exact duration).
// ---------------------------------------------------------------------------
TEST_CASE("AUD-006/007: seekToTime edge cases", "[audio][edge]") {
    auto wavPath = uniqueTempPath(".wav");
    TempFileGuard guard(wavPath);
    writeMinimalWav(wavPath, 8000, 1, 8000); // 1-second file

    FfmpegAudioDecoder decoder;
    REQUIRE(decoder.open(wavPath.string()).hasValue());

    SECTION("AUD-006: negative seconds -> Error or clamp to 0") {
        auto result = decoder.seekToTime(-1.0);
        // Matrix allows returning Error or clamping to 0; both are acceptable.
        if (result.hasValue()) {
            // After clamping to 0, read should return data
            auto rd = decoder.read(1024);
            REQUIRE(rd.hasValue());
        } else {
            REQUIRE_FALSE(result.hasValue());
        }
    }

    SECTION("AUD-007: beyond duration -> Error or seek to EOF") {
        auto result = decoder.seekToTime(100.0);
        if (result.hasValue()) {
            // After seeking to EOF, read should return an empty buffer
            auto rd = decoder.read(1024);
            REQUIRE(rd.hasValue());
            REQUIRE(rd->empty());
        } else {
            // Beyond duration returns Error
            REQUIRE_FALSE(result.hasValue());
        }
    }

    SECTION("AUD-006b: seekToTime(0.0) returns to start") {
        // Boundary: seeking to 0.0 must succeed and reset the read pointer
        REQUIRE(decoder.seekToTime(0.0).hasValue());
        auto rd = decoder.read(1024);
        REQUIRE(rd.hasValue());
        REQUIRE_FALSE(rd->empty());
    }

    SECTION("AUD-007b: seekToTime(exact duration) is accepted") {
        // Boundary: seeking to the exact duration (1.0s) must not crash;
        // either succeeds (subsequent read returns empty) or returns Error.
        auto result = decoder.seekToTime(1.0);
        if (result.hasValue()) {
            auto rd = decoder.read(1024);
            REQUIRE(rd.hasValue());
        } else {
            REQUIRE_FALSE(result.hasValue());
        }
    }
}

// ---------------------------------------------------------------------------
// AUD-008: AudioBuffer::create with 0 channelCount
// create does not return Error; it returns a degenerate buffer with byteSize=0
// and does not crash.
// ---------------------------------------------------------------------------
TEST_CASE("AUD-008: create with zero channelCount", "[audio][edge]") {
    auto buf = AudioBuffer::create(100, 0, SampleFormat::Float32);
    // No crash; channelCount=0 results in byteSize=0
    REQUIRE(buf.frameCount() == 100);
    REQUIRE(buf.channelCount() == 0);
    REQUIRE(buf.byteSize() == 0);
}

// ---------------------------------------------------------------------------
// AUD-009: AudioBuffer::fromView with nullptr data
// fromView with null data creates a view buffer with an empty span; no crash.
// ---------------------------------------------------------------------------
TEST_CASE("AUD-009: fromView with nullptr data does not crash", "[audio][edge]") {
    AudioBuffer buf;
    REQUIRE_NOTHROW(buf = AudioBuffer::fromView(nullptr, 100, 2, SampleFormat::Float32));
    // Construction does not crash; when data is null the internal storage is an empty span
    REQUIRE(buf.isView());
    REQUIRE(buf.byteSize() == 0);
}

// ---------------------------------------------------------------------------
// AUD-010: AudioBuffer::slice with startFrame out of range
// slice() guards out-of-bounds startFrame with assert. Release (NDEBUG) does
// defensive clamping and returns an empty buffer; Debug aborts via assert.
// Also verifies the clamping behavior for a valid startFrame with an excessive
// frameCount (safe in both build modes).
// ---------------------------------------------------------------------------
TEST_CASE("AUD-010: slice with startFrame out of range", "[audio][edge]") {
    auto buf = AudioBuffer::create(100, 2, SampleFormat::Float32);

    SECTION("valid start, excessive frameCount clamps") {
        // startFrame=50 is valid, frameCount=100 exceeds available (50); clamp to 50
        auto sliced = buf.slice(50, 100);
        REQUIRE(sliced.frameCount() == 50);
        REQUIRE(sliced.channelCount() == 2);
    }

#ifdef NDEBUG
    SECTION("out-of-range startFrame returns empty in Release") {
        // Release: assert is removed, availFrames = 100-200 < 0, frameCount<=0
        // returns an empty buffer (matches matrix "return empty buffer")
        auto sliced = buf.slice(200, 50);
        REQUIRE(sliced.empty());
    }
#else
    SECTION("out-of-range startFrame is assert-guarded in Debug") {
        SKIP("Debug builds guard slice() out-of-range startFrame with assert "
             "(abort). Release builds (NDEBUG) return empty buffer per matrix.");
    }
#endif
}

// ---------------------------------------------------------------------------
// AUD-011: AudioBuffer::sampleAt with invalid channel
// sampleAt() guards out-of-bounds channel with assert (AudioBuffer.cpp:114:
// `assert(channel >= 0 && channel < m_channelCount)`). Matrix allows "return 0
// or assert". Debug aborts via assert; Release reads out-of-bounds (UB).
// This is the same UB category as AUD-012/013 (assert-guarded format/range
// mismatches): the invalid call cannot be exercised without aborting the test
// process in Debug or invoking UB in Release. The valid-channel SECTION below
// runs as a reachable baseline; the invalid-channel SECTION is SKIP per the UB
// policy (consistent with AUD-012/013/038).
// ---------------------------------------------------------------------------
TEST_CASE("AUD-011: sampleAt with invalid channel", "[audio][edge]") {
    // Construct a 2-channel Float32 buffer and write known values
    constexpr int64_t frames = 10;
    constexpr int channels = 2;
    auto buf = AudioBuffer::create(frames, channels, SampleFormat::Float32);
    auto span = buf.floats();
    REQUIRE(span.size() == static_cast<size_t>(frames * channels));
    for (size_t i = 0; i < span.size(); ++i) {
        span[i] = static_cast<float>(i);
    }

    SECTION("valid channel access returns correct sample") {
        // frame=0, channel=0 -> index 0 -> 0.0f
        REQUIRE(buf.sampleAt(0, 0) == 0.0f);
        // frame=0, channel=1 -> index 1 -> 1.0f
        REQUIRE(buf.sampleAt(0, 1) == 1.0f);
    }

    SECTION("invalid channel is assert-guarded") {
        // channel=5 exceeds the 2-channel range. sampleAt guards with
        // assert(channel < m_channelCount) (AudioBuffer.cpp:114): Debug aborts
        // the process, Release reads out-of-bounds (UB). Matrix allows "return
        // 0 or assert"; the implementation chose assert. Same UB category as
        // AUD-012/013 — cannot be exercised without aborting (Debug) or
        // invoking UB (Release).
        SKIP("sampleAt() guards invalid channel with assert (AudioBuffer.cpp:114, "
             "Debug abort). Release reads out-of-bounds (UB). Matrix allows assert; "
             "cannot test safely without aborting the test process. Same UB "
             "category as AUD-012/013/038; valid-channel baseline runs above.");
    }
}

// ---------------------------------------------------------------------------
// AUD-012: AudioBuffer::floats called on an Int16 format
// floats() guards format mismatch with assert. Matrix allows "return empty span
// or assert". Debug aborts via assert; Release reinterprets with the wrong
// format (UB). SKIP the invalid call and verify valid Float32 access as a baseline.
// ---------------------------------------------------------------------------
TEST_CASE("AUD-012: floats on Int16 buffer is assert-guarded", "[audio][edge]") {
    SECTION("floats on Float32 buffer returns valid span") {
        auto buf = AudioBuffer::create(10, 2, SampleFormat::Float32);
        auto span = buf.floats();
        REQUIRE(span.size() == 20);
    }

    SECTION("floats on Int16 buffer is assert-guarded") {
        // Calling floats() on an Int16 buffer triggers assert(m_format == Float32).
        // Debug aborts; Release reinterprets int16 data as float (out-of-bounds read UB).
        // Matrix allows "return empty span or assert".
        SKIP("floats() guards format mismatch with assert (Debug abort). "
             "Release reinterprets Int16 as Float32 (OOB UB). Matrix allows "
             "assert; cannot test safely without aborting the test process.");
    }
}

// ---------------------------------------------------------------------------
// AUD-013: AudioBuffer view mode with source data released
// Matrix expected "documented as UB". view does not own the data; accessing
// after the source is released is a dangling-reference UB.
// ---------------------------------------------------------------------------
TEST_CASE("AUD-013: view buffer with released source data is UB", "[audio][edge]") {
    SKIP("P2/UB: fromView creates a non-owning span; releasing the source "
         "data leaves the view with a dangling reference. Accessing it is "
         "undefined behavior. Documented per matrix.");
}

// ===========================================================================
// AUD-014 ~ AUD-020: supplementary edge coverage for AudioBuffer default state,
// zero-frame/zero-channel construction, and clone/zero/durationSec/byteSize/
// isView accessors. Uses only the AudioBuffer.h public API.
// ===========================================================================

// ---------------------------------------------------------------------------
// AUD-014: default-constructed AudioBuffer is empty
// A default-constructed buffer has frameCount=0, channelCount=0,
// format=Unknown, empty=true.
// ---------------------------------------------------------------------------
TEST_CASE("AUD-014: default-constructed AudioBuffer is empty", "[audio][edge]") {
    AudioBuffer buf;
    REQUIRE(buf.empty());
    REQUIRE(buf.frameCount() == 0);
    REQUIRE(buf.channelCount() == 0);
    REQUIRE(buf.format() == SampleFormat::Unknown);
}

// ---------------------------------------------------------------------------
// AUD-015: AudioBuffer::create(0, ...) returns an empty buffer
// Zero-frame construction must not crash; empty() is true, frameCount() is 0.
// ---------------------------------------------------------------------------
TEST_CASE("AUD-015: AudioBuffer::create with zero frames returns empty",
          "[audio][edge]") {
    auto buf = AudioBuffer::create(0, 2, SampleFormat::Float32);
    REQUIRE(buf.empty());
    REQUIRE(buf.frameCount() == 0);
    REQUIRE(buf.channelCount() == 2); // channel count is preserved
    REQUIRE(buf.format() == SampleFormat::Float32);
}

// ---------------------------------------------------------------------------
// AUD-016: AudioBuffer::clone on empty buffer returns empty
// Defensive test: clone() must not crash on an empty buffer.
// ---------------------------------------------------------------------------
TEST_CASE("AUD-016: AudioBuffer::clone on empty buffer returns empty",
          "[audio][edge]") {
    AudioBuffer empty;
    auto cloned = empty.clone();
    REQUIRE(cloned.empty());
    REQUIRE(cloned.frameCount() == 0);
}

// ---------------------------------------------------------------------------
// AUD-017: AudioBuffer::zero fills owned buffer with zeros
// Construct a Float32 buffer with non-zero values; after calling zero(),
// floats() must all be 0.0f.
// ---------------------------------------------------------------------------
TEST_CASE("AUD-017: AudioBuffer::zero fills owned buffer with zeros",
          "[audio][edge]") {
    auto buf = AudioBuffer::create(10, 2, SampleFormat::Float32);
    auto span = buf.floats();
    for (auto &s : span) {
        s = 1.0f;
    }
    buf.zero();
    for (const auto s : buf.floats()) {
        REQUIRE(s == 0.0f);
    }
}

// ---------------------------------------------------------------------------
// AUD-018: AudioBuffer::durationSec behavior with zero sample rate
// Passing sampleRate=0 should avoid division by zero (return 0 or inf; no crash).
// ---------------------------------------------------------------------------
TEST_CASE("AUD-018: AudioBuffer::durationSec with zero sample rate",
          "[audio][edge]") {
    auto buf = AudioBuffer::create(100, 1, SampleFormat::Float32);
    // Zero sample rate: implementation may return 0 or inf; this test only
    // verifies no crash.
    REQUIRE_NOTHROW(buf.durationSec(0));
    // Normal sample rate: 100 frames / 100 Hz = 1.0 second
    REQUIRE(buf.durationSec(100) == 1.0);
}

// ---------------------------------------------------------------------------
// AUD-019: AudioBuffer::byteSize on empty buffer is zero
// Defensive test: byteSize of an empty buffer must be 0; no crash.
// ---------------------------------------------------------------------------
TEST_CASE("AUD-019: AudioBuffer::byteSize on empty buffer is zero",
          "[audio][edge]") {
    AudioBuffer empty;
    REQUIRE(empty.byteSize() == 0);
}

// ---------------------------------------------------------------------------
// AUD-020: AudioBuffer::isView distinguishes view vs owned buffers
// create() constructs an owned buffer, fromView() constructs a view buffer.
// isView() should return false and true respectively.
// ---------------------------------------------------------------------------
TEST_CASE("AUD-020: AudioBuffer::isView distinguishes view vs owned",
          "[audio][edge]") {
    // owned buffer
    auto owned = AudioBuffer::create(10, 1, SampleFormat::Float32);
    REQUIRE_FALSE(owned.isView());

    // view buffer: references an external memory region
    std::vector<float> external(10, 0.0f);
    auto viewed = AudioBuffer::fromView(external.data(), 10, 1, SampleFormat::Float32);
    REQUIRE(viewed.isView());
    REQUIRE(viewed.frameCount() == 10);
}

// ===========================================================================
// AUD-021 ~ AUD-030: third round of extended edge cases (slice boundaries,
// fromCopy/fromVector semantics, clone preservation, Int16 format access,
// durationSec boundaries). All cases use only APIs declared in AudioBuffer.h /
// SampleFormat.h.
// ===========================================================================

// ---------------------------------------------------------------------------
// AUD-021: AudioBuffer::slice from startFrame=0 returns a full copy
// Verify slice(0, frameCount) returns a copy equal in size to the original buffer.
// ---------------------------------------------------------------------------
TEST_CASE("AUD-021: AudioBuffer::slice from startFrame=0 returns full copy",
          "[audio][edge]") {
    auto buf = AudioBuffer::create(100, 2, SampleFormat::Float32);
    // Fill with distinguishable data
    auto span = buf.floats();
    for (size_t i = 0; i < span.size(); ++i) {
        span[i] = static_cast<float>(i);
    }
    auto sliced = buf.slice(0, 100);
    REQUIRE(sliced.frameCount() == 100);
    REQUIRE(sliced.channelCount() == 2);
    REQUIRE(sliced.format() == SampleFormat::Float32);
    // slice in owned mode is a copy
    REQUIRE_FALSE(sliced.isView());
}

// ---------------------------------------------------------------------------
// AUD-022: AudioBuffer::slice clamps frameCount when exceeding available frames
// slice(95, 50) on a 100-frame buffer should clamp to 5 frames.
// ---------------------------------------------------------------------------
TEST_CASE("AUD-022: AudioBuffer::slice clamps frameCount to available",
          "[audio][edge]") {
    auto buf = AudioBuffer::create(100, 1, SampleFormat::Float32);
    auto sliced = buf.slice(95, 50);
    // Clamped to 100-95=5 frames
    REQUIRE(sliced.frameCount() == 5);
    REQUIRE(sliced.channelCount() == 1);
}

// ---------------------------------------------------------------------------
// AUD-023: AudioBuffer::slice requesting 0 frames returns an empty buffer
// slice(0, 0) should return an empty buffer; no crash.
// ---------------------------------------------------------------------------
TEST_CASE("AUD-023: AudioBuffer::slice with frameCount=0 returns empty",
          "[audio][edge]") {
    auto buf = AudioBuffer::create(100, 1, SampleFormat::Float32);
    auto sliced = buf.slice(0, 0);
    REQUIRE(sliced.empty());
    REQUIRE(sliced.frameCount() == 0);
}

// ---------------------------------------------------------------------------
// AUD-024: AudioBuffer::fromCopy with nullptr data creates a zeroed buffer
// fromCopy(nullptr, N, ch, fmt) should construct an owned N-frame buffer
// (data is zero).
// ---------------------------------------------------------------------------
TEST_CASE("AUD-024: AudioBuffer::fromCopy with nullptr data creates zeroed buffer",
          "[audio][edge]") {
    auto buf = AudioBuffer::fromCopy(nullptr, 10, 1, SampleFormat::Float32);
    REQUIRE(buf.frameCount() == 10);
    REQUIRE(buf.channelCount() == 1);
    REQUIRE_FALSE(buf.isView());
    // Data should be zero-initialized (the vector allocated by create is
    // default zero-initialized)
    auto span = buf.floats();
    for (size_t i = 0; i < span.size(); ++i) {
        REQUIRE(span[i] == 0.0f);
    }
}

// ---------------------------------------------------------------------------
// AUD-025: AudioBuffer::fromVector moves data from the source vector
// fromVector(std::move(data), ...) should transfer ownership; the source
// vector becomes empty.
// ---------------------------------------------------------------------------
TEST_CASE("AUD-025: AudioBuffer::fromVector moves data from source vector",
          "[audio][edge]") {
    std::vector<uint8_t> data(40, 0xAB); // 10 frames * 1 ch * 4 bytes (Float32)
    auto buf = AudioBuffer::fromVector(std::move(data), 10, 1, SampleFormat::Float32);
    REQUIRE(buf.frameCount() == 10);
    REQUIRE(buf.channelCount() == 1);
    REQUIRE(buf.format() == SampleFormat::Float32);
    REQUIRE_FALSE(buf.isView());
    // The source vector has been moved from and should be empty
    REQUIRE(data.empty());
}

// ---------------------------------------------------------------------------
// AUD-026: AudioBuffer::clone preserves all attributes
// clone() should return a new owned buffer with matching frameCount/
// channelCount/format.
// ---------------------------------------------------------------------------
TEST_CASE("AUD-026: AudioBuffer::clone preserves all attributes",
          "[audio][edge]") {
    auto buf = AudioBuffer::create(50, 2, SampleFormat::Float32);
    auto span = buf.floats();
    for (size_t i = 0; i < span.size(); ++i) {
        span[i] = static_cast<float>(i * 0.1f);
    }
    auto cloned = buf.clone();
    REQUIRE(cloned.frameCount() == 50);
    REQUIRE(cloned.channelCount() == 2);
    REQUIRE(cloned.format() == SampleFormat::Float32);
    REQUIRE_FALSE(cloned.isView());
    // Data should match
    auto origSpan = buf.floats();
    auto cloneSpan = cloned.floats();
    REQUIRE(origSpan.size() == cloneSpan.size());
    for (size_t i = 0; i < origSpan.size(); ++i) {
        REQUIRE(origSpan[i] == cloneSpan[i]);
    }
}

// ---------------------------------------------------------------------------
// AUD-027: AudioBuffer Int16 format int16s() access
// An Int16 format buffer should be accessible via int16s().
// ---------------------------------------------------------------------------
TEST_CASE("AUD-027: AudioBuffer int16s() accesses Int16 format data",
          "[audio][edge]") {
    std::vector<int16_t> data(10, 42);
    // Use const so int16s() resolves to the const overload, which exposes
    // the view's data via dataPtr(). The non-const overload returns empty
    // for view buffers because dataPtrMutable() refuses to hand out a
    // write pointer to non-owned data (view buffers are immutable).
    const auto buf = AudioBuffer::fromView(data.data(), 10, 1, SampleFormat::Int16);
    REQUIRE(buf.format() == SampleFormat::Int16);
    auto span = buf.int16s();
    REQUIRE(span.size() == 10);
    for (size_t i = 0; i < span.size(); ++i) {
        REQUIRE(span[i] == 42);
    }
}

// ---------------------------------------------------------------------------
// AUD-028: AudioBuffer::durationSec calculation with different sample rates
// durationSec should correctly compute frameCount / sampleRate.
// ---------------------------------------------------------------------------
TEST_CASE("AUD-028: AudioBuffer::durationSec calculates correctly",
          "[audio][edge]") {
    auto buf = AudioBuffer::create(44100, 1, SampleFormat::Float32);
    REQUIRE(buf.durationSec(44100) == 1.0);
    REQUIRE(buf.durationSec(22050) == 2.0);
    // Negative sample rate returns 0.0 (implementation has a sampleRate <= 0 guard)
    REQUIRE(buf.durationSec(-1) == 0.0);
    REQUIRE(buf.durationSec(0) == 0.0);
}

// ---------------------------------------------------------------------------
// AUD-029: AudioBuffer slice on a view buffer stays a view
// A buffer created via fromView should preserve view semantics in its slice
// (zero-copy sub-span).
// ---------------------------------------------------------------------------
TEST_CASE("AUD-029: AudioBuffer slice on view buffer stays as view",
          "[audio][edge]") {
    std::vector<float> data(100, 1.0f);
    auto viewBuf = AudioBuffer::fromView(data.data(), 100, 1, SampleFormat::Float32);
    REQUIRE(viewBuf.isView());
    auto sliced = viewBuf.slice(10, 20);
    // A slice of a view buffer is still a view
    REQUIRE(sliced.isView());
    REQUIRE(sliced.frameCount() == 20);
}

// ---------------------------------------------------------------------------
// AUD-030: AudioBuffer Int32 format construction and rawData access
// An Int32 format buffer should construct correctly and be accessible via
// rawData for bytes.
// ---------------------------------------------------------------------------
TEST_CASE("AUD-030: AudioBuffer Int32 format construction and rawData",
          "[audio][edge]") {
    auto buf = AudioBuffer::create(10, 1, SampleFormat::Int32);
    REQUIRE(buf.format() == SampleFormat::Int32);
    REQUIRE(buf.frameCount() == 10);
    // byteSize = 10 frames * 1 ch * 4 bytes/sample = 40
    REQUIRE(buf.byteSize() == 40);
    const uint8_t *raw = buf.rawData();
    REQUIRE(raw != nullptr);
}

// ===========================================================================
// AUD-031 ~ AUD-038: regression tests for recently fixed AudioBuffer bugs.
// Covers negative-dimension guards in create/fromCopy/fromView, zero-frame
// slice behavior, fromVector size-mismatch handling, and sampleAt on empty
// buffers. Uses only APIs declared in AudioBuffer.h / SampleFormat.h.
// ===========================================================================

// ---------------------------------------------------------------------------
// AUD-031: AudioBuffer::create with negative frameCount returns empty buffer
// Regression: create() must guard against negative frameCount and return a
// default-constructed (empty) buffer instead of crashing or producing a
// negative-sized allocation (lib/Audio/src/AudioBuffer.cpp:26).
// ---------------------------------------------------------------------------
TEST_CASE("AUD-031: create with negative frameCount returns empty",
          "[audio][edge]") {
    auto buf = AudioBuffer::create(-1, 1, SampleFormat::Float32);
    REQUIRE(buf.empty());
    REQUIRE(buf.frameCount() == 0);
    REQUIRE(buf.channelCount() == 0);
    REQUIRE(buf.format() == SampleFormat::Unknown);
    REQUIRE(buf.byteSize() == 0);
}

// ---------------------------------------------------------------------------
// AUD-032: AudioBuffer::create with negative channelCount returns empty buffer
// Regression: create() must guard against negative channelCount and return a
// default-constructed (empty) buffer instead of crashing
// (lib/Audio/src/AudioBuffer.cpp:26).
// ---------------------------------------------------------------------------
TEST_CASE("AUD-032: create with negative channelCount returns empty",
          "[audio][edge]") {
    auto buf = AudioBuffer::create(10, -1, SampleFormat::Float32);
    REQUIRE(buf.empty());
    REQUIRE(buf.frameCount() == 0);
    REQUIRE(buf.channelCount() == 0);
    REQUIRE(buf.format() == SampleFormat::Unknown);
    REQUIRE(buf.byteSize() == 0);
}

// ---------------------------------------------------------------------------
// AUD-033: AudioBuffer::fromCopy with negative dimensions returns empty buffer
// Regression: fromCopy() must guard against negative frameCount/channelCount
// and return an empty buffer instead of crashing or performing a negative-
// sized memcpy (lib/Audio/src/AudioBuffer.cpp:42).
// ---------------------------------------------------------------------------
TEST_CASE("AUD-033: fromCopy with negative dimensions returns empty",
          "[audio][edge]") {
    SECTION("negative frameCount") {
        float src[10] = {};
        auto buf = AudioBuffer::fromCopy(src, -1, 1, SampleFormat::Float32);
        REQUIRE(buf.empty());
        REQUIRE(buf.frameCount() == 0);
        REQUIRE(buf.channelCount() == 0);
        REQUIRE(buf.format() == SampleFormat::Unknown);
    }

    SECTION("negative channelCount") {
        float src[10] = {};
        auto buf = AudioBuffer::fromCopy(src, 10, -1, SampleFormat::Float32);
        REQUIRE(buf.empty());
        REQUIRE(buf.frameCount() == 0);
        REQUIRE(buf.channelCount() == 0);
        REQUIRE(buf.format() == SampleFormat::Unknown);
    }
}

// ---------------------------------------------------------------------------
// AUD-034: AudioBuffer::fromView with negative dimensions returns empty buffer
// Regression: fromView() must guard against negative frameCount/channelCount
// and return an empty buffer instead of crashing or constructing a negative-
// sized span (lib/Audio/src/AudioBuffer.cpp:53).
// ---------------------------------------------------------------------------
TEST_CASE("AUD-034: fromView with negative dimensions returns empty",
          "[audio][edge]") {
    SECTION("negative frameCount") {
        float src[10] = {};
        auto buf = AudioBuffer::fromView(src, -1, 1, SampleFormat::Float32);
        REQUIRE(buf.empty());
        REQUIRE(buf.frameCount() == 0);
        REQUIRE(buf.channelCount() == 0);
        REQUIRE(buf.format() == SampleFormat::Unknown);
    }

    SECTION("negative channelCount") {
        float src[10] = {};
        auto buf = AudioBuffer::fromView(src, 10, -1, SampleFormat::Float32);
        REQUIRE(buf.empty());
        REQUIRE(buf.frameCount() == 0);
        REQUIRE(buf.channelCount() == 0);
        REQUIRE(buf.format() == SampleFormat::Unknown);
    }
}

// ---------------------------------------------------------------------------
// AUD-035: AudioBuffer::create with zero frameCount returns valid empty buffer
// Regression: create(0, ...) must return a valid (non-default) buffer that
// preserves channelCount and format while reporting empty() == true and
// frameCount() == 0. This contrasts with the negative-dimension guard path
// (AUD-031/032) which returns a fully default-constructed buffer.
// ---------------------------------------------------------------------------
TEST_CASE("AUD-035: create with zero frames returns valid empty buffer",
          "[audio][edge]") {
    auto buf = AudioBuffer::create(0, 1, SampleFormat::Float32);
    REQUIRE(buf.empty());
    REQUIRE(buf.frameCount() == 0);
    // channelCount and format are preserved (unlike the negative-dimension
    // guard path which returns a fully default-constructed buffer).
    REQUIRE(buf.channelCount() == 1);
    REQUIRE(buf.format() == SampleFormat::Float32);
    REQUIRE(buf.byteSize() == 0);
    REQUIRE_FALSE(buf.isView());
}

// ---------------------------------------------------------------------------
// AUD-036: AudioBuffer slice on buffer with 0 frames returns empty
// Regression: slice() on a 0-frame buffer must not crash. In Release builds
// (NDEBUG) the defensive clamping returns an empty buffer. Debug builds guard
// the startFrame bound with assert (startFrame < m_frameCount -> 0 < 0) and
// abort; same pattern as AUD-010.
// ---------------------------------------------------------------------------
TEST_CASE("AUD-036: slice on zero-frame buffer returns empty",
          "[audio][edge]") {
    auto buf = AudioBuffer::create(0, 1, SampleFormat::Float32);
    REQUIRE(buf.empty());

#ifdef NDEBUG
    SECTION("Release: slice returns empty buffer") {
        // Release: assert is removed; availFrames = 0 - 0 = 0, frameCount
        // clamps to 0, returns an empty buffer (matches matrix).
        auto sliced = buf.slice(0, 10);
        REQUIRE(sliced.empty());
        REQUIRE(sliced.frameCount() == 0);
    }
#else
    SECTION("Debug: slice is assert-guarded") {
        SKIP("Debug builds guard slice() startFrame < m_frameCount with "
             "assert (abort) even for a 0-frame buffer. Release builds "
             "(NDEBUG) return an empty buffer per matrix.");
    }
#endif
}

// ---------------------------------------------------------------------------
// AUD-037: AudioBuffer::fromVector with mismatched data size
// Regression: fromVector() does not validate that data.size() matches
// frameCount*channelCount*bytesPerSample (lib/Audio/src/AudioBuffer.cpp:68).
// Construction must not crash; the buffer stores the provided data and
// metadata as-is. byteSize() reflects the actual vector size, not the
// computed frame layout. Data accessors are not invoked here because they
// would read out-of-bounds (UB).
// ---------------------------------------------------------------------------
TEST_CASE("AUD-037: fromVector with mismatched data size does not crash",
          "[audio][edge]") {
    // Specify 10 frames * 1 ch * 4 bytes (Float32) = 40 bytes expected,
    // but provide only 8 bytes of data.
    std::vector<uint8_t> data(8, 0x5A);
    AudioBuffer buf;
    REQUIRE_NOTHROW(buf = AudioBuffer::fromVector(std::move(data), 10, 1,
                                                  SampleFormat::Float32));
    // fromVector does not validate size; metadata is stored as-is.
    REQUIRE(buf.frameCount() == 10);
    REQUIRE(buf.channelCount() == 1);
    REQUIRE(buf.format() == SampleFormat::Float32);
    REQUIRE_FALSE(buf.isView());
    // byteSize reflects the actual vector size, not the frame layout
    REQUIRE(buf.byteSize() == 8);
}

// ---------------------------------------------------------------------------
// AUD-038: AudioBuffer sampleAt on empty buffer returns 0 or does not crash
// Regression: sampleAt() on an empty buffer is guarded by asserts on format
// and frame/channel bounds (lib/Audio/src/AudioBuffer.cpp:112). Debug aborts
// via assert; Release dereferences the empty data pointer (UB). Matrix allows
// "return 0 or assert". SKIP the unsafe call and verify valid sampleAt access
// as a baseline (same pattern as AUD-011/AUD-012).
// ---------------------------------------------------------------------------
TEST_CASE("AUD-038: sampleAt on empty buffer is assert-guarded",
          "[audio][edge]") {
    SECTION("valid sampleAt access returns correct value") {
        auto buf = AudioBuffer::create(10, 2, SampleFormat::Float32);
        auto span = buf.floats();
        span[0] = 0.5f;
        span[1] = -0.25f;
        REQUIRE(buf.sampleAt(0, 0) == 0.5f);
        REQUIRE(buf.sampleAt(0, 1) == -0.25f);
    }

    SECTION("sampleAt on empty buffer is assert-guarded") {
        // On a default-constructed (empty) buffer, sampleAt triggers:
        //   assert(m_format == Float32)         -- format is Unknown
        //   assert(frame >= 0 && frame < 0)     -- 0 < 0 is false
        //   assert(channel >= 0 && channel < 0) -- 0 < 0 is false
        // Debug aborts via assert; Release dereferences the empty vector's
        // data pointer (nullptr/empty) which is UB. Matrix allows "return 0
        // or assert"; cannot test safely without aborting the process.
        SKIP("sampleAt() on an empty buffer is guarded by format and bounds "
             "asserts (Debug abort). Release dereferences an empty data "
             "pointer (UB). Matrix allows assert; cannot test safely.");
    }
}
