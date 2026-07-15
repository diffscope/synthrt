// Unit tests for ds::infer::inferutil::preprocessSpeakerEmbeddingFrames.
//
// Simulates real ds-editor-lite speaker mix patterns:
//   - convertInputSpeakers: InputSpeakerInfo{name, interval, proportions}
//   - preprocessSpeakerEmbeddingFrames: mix proportions × embedding vectors
//   - Real values: hiddenSize=256, frameWidth=0.01, proportions in [0,1]
//   - Static speaker: proportions={1.0}, interval=0
//   - Dynamic speaker mix: proportions vary over time

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include <inferutil/SpeakerEmbedding.h>

using namespace ds::infer::inferutil;
namespace Co = srt::svs::Api::Common::L1;

namespace {
    bool approxEqual(float a, float b, float eps = 1e-5f) {
        return std::abs(a - b) < eps;
    }

    // Create a simple embedding vector where all values = fillValue
    std::vector<float> makeEmbedding(int hiddenSize, float fillValue) {
        return std::vector<float>(hiddenSize, fillValue);
    }

    // Create a static speaker (proportion=1.0 everywhere)
    Co::InputSpeakerInfo makeStaticSpeaker(const std::string &name) {
        Co::InputSpeakerInfo spk;
        spk.name = name;
        spk.interval = 0;
        spk.proportions = {1.0};
        return spk;
    }
}

// ---------------------------------------------------------------------------
// Basic preprocessSpeakerEmbeddingFrames tests
// ---------------------------------------------------------------------------

TEST_CASE("preprocessSpeakerEmbedding single static speaker", "[speakerembedding]") {
    int hiddenSize = 4;
    int64_t targetLength = 10;
    double frameWidth = 0.01;

    auto speakers = std::vector<Co::InputSpeakerInfo>{makeStaticSpeaker("s1")};
    auto embMap = std::map<std::string, std::vector<float>>{
        {"s1", makeEmbedding(hiddenSize, 1.0f)}
    };

    auto exp = preprocessSpeakerEmbeddingFrames(speakers, embMap, hiddenSize, frameWidth, targetLength);
    REQUIRE(exp.hasValue());
    auto tensor = exp.take();
    REQUIRE(tensor);
    REQUIRE(tensor->elementCount() == static_cast<size_t>(targetLength * hiddenSize));

    auto view = tensor->view<float>();
    // All frames should be 1.0 * 1.0 = 1.0
    for (int64_t i = 0; i < targetLength; ++i) {
        for (int j = 0; j < hiddenSize; ++j) {
            REQUIRE(approxEqual(view[i * hiddenSize + j], 1.0f));
        }
    }
}

TEST_CASE("preprocessSpeakerEmbedding two static speakers mix", "[speakerembedding]") {
    // Two speakers each with proportion=1.0 -> embedding = emb1 + emb2
    int hiddenSize = 4;
    int64_t targetLength = 5;
    double frameWidth = 0.01;

    auto speakers = std::vector<Co::InputSpeakerInfo>{
        makeStaticSpeaker("s1"), makeStaticSpeaker("s2")
    };
    auto embMap = std::map<std::string, std::vector<float>>{
        {"s1", makeEmbedding(hiddenSize, 1.0f)},
        {"s2", makeEmbedding(hiddenSize, 2.0f)}
    };

    auto exp = preprocessSpeakerEmbeddingFrames(speakers, embMap, hiddenSize, frameWidth, targetLength);
    REQUIRE(exp.hasValue());
    auto tensor = exp.take();
    auto view = tensor->view<float>();
    // Each frame: 1.0*1.0 + 1.0*2.0 = 3.0
    for (int64_t i = 0; i < targetLength; ++i) {
        for (int j = 0; j < hiddenSize; ++j) {
            REQUIRE(approxEqual(view[i * hiddenSize + j], 3.0f));
        }
    }
}

TEST_CASE("preprocessSpeakerEmbedding dynamic speaker proportions", "[speakerembedding]") {
    // Speaker with varying proportions over time
    int hiddenSize = 2;
    int64_t targetLength = 10;
    double frameWidth = 0.01;

    Co::InputSpeakerInfo spk;
    spk.name = "s1";
    spk.interval = 0.02; // 20ms interval
    spk.proportions = {0.0, 0.5, 1.0}; // ramp from 0 to 1 over 3 points at 20ms

    auto speakers = std::vector<Co::InputSpeakerInfo>{spk};
    auto embMap = std::map<std::string, std::vector<float>>{
        {"s1", makeEmbedding(hiddenSize, 1.0f)}
    };

    auto exp = preprocessSpeakerEmbeddingFrames(speakers, embMap, hiddenSize, frameWidth, targetLength);
    REQUIRE(exp.hasValue());
    auto tensor = exp.take();
    auto view = tensor->view<float>();
    // First frame should be 0 (proportion starts at 0)
    REQUIRE(approxEqual(view[0], 0.0f));
    // Last frame should be ~1.0 (proportion reaches 1.0)
    REQUIRE(view[(targetLength - 1) * hiddenSize] > 0.5f);
}

TEST_CASE("preprocessSpeakerEmbedding unknown speaker returns error", "[speakerembedding]") {
    int hiddenSize = 4;
    auto speakers = std::vector<Co::InputSpeakerInfo>{makeStaticSpeaker("unknown")};
    auto embMap = std::map<std::string, std::vector<float>>{};

    auto exp = preprocessSpeakerEmbeddingFrames(speakers, embMap, hiddenSize, 0.01, 10);
    REQUIRE(!exp.hasValue());
}

TEST_CASE("preprocessSpeakerEmbedding size mismatch returns error", "[speakerembedding]") {
    // Embedding vector size doesn't match hiddenSize
    int hiddenSize = 4;
    auto speakers = std::vector<Co::InputSpeakerInfo>{makeStaticSpeaker("s1")};
    auto embMap = std::map<std::string, std::vector<float>>{
        {"s1", std::vector<float>(8, 1.0f)} // 8 != hiddenSize(4)
    };

    auto exp = preprocessSpeakerEmbeddingFrames(speakers, embMap, hiddenSize, 0.01, 10);
    REQUIRE(!exp.hasValue());
}

TEST_CASE("preprocessSpeakerEmbedding zero target length", "[speakerembedding]") {
    int hiddenSize = 4;
    int64_t targetLength = 0;

    auto speakers = std::vector<Co::InputSpeakerInfo>{makeStaticSpeaker("s1")};
    auto embMap = std::map<std::string, std::vector<float>>{
        {"s1", makeEmbedding(hiddenSize, 1.0f)}
    };

    auto exp = preprocessSpeakerEmbeddingFrames(speakers, embMap, hiddenSize, 0.01, targetLength);
    // Zero-length tensor: may succeed with empty buffer or fail
    // Just verify no crash
    if (exp.hasValue()) {
        auto tensor = exp.take();
        // elementCount should be 0
    }
}

TEST_CASE("preprocessSpeakerEmbedding empty speakers list", "[speakerembedding]") {
    // No speakers -> all zeros (no mixing happens)
    int hiddenSize = 4;
    auto speakers = std::vector<Co::InputSpeakerInfo>{};
    auto embMap = std::map<std::string, std::vector<float>>{};

    auto exp = preprocessSpeakerEmbeddingFrames(speakers, embMap, hiddenSize, 0.01, 10);
    REQUIRE(exp.hasValue());
    auto tensor = exp.take();
    auto view = tensor->view<float>();
    // All values should be 0 (no mixing)
    for (size_t i = 0; i < view.size(); ++i) {
        REQUIRE(approxEqual(view[i], 0.0f));
    }
}

TEST_CASE("preprocessSpeakerEmbedding real hiddenSize 256", "[speakerembedding][realworld]") {
    // Real DiffSinger models use hiddenSize=256
    int hiddenSize = 256;
    int64_t targetLength = 100;
    double frameWidth = 0.01;

    auto speakers = std::vector<Co::InputSpeakerInfo>{makeStaticSpeaker("s1")};
    auto embMap = std::map<std::string, std::vector<float>>{
        {"s1", makeEmbedding(hiddenSize, 0.5f)}
    };

    auto exp = preprocessSpeakerEmbeddingFrames(speakers, embMap, hiddenSize, frameWidth, targetLength);
    REQUIRE(exp.hasValue());
    auto tensor = exp.take();
    REQUIRE(tensor->elementCount() == static_cast<size_t>(targetLength * hiddenSize));
    auto view = tensor->view<float>();
    // All values should be 1.0 * 0.5 = 0.5
    REQUIRE(approxEqual(view[0], 0.5f));
    REQUIRE(approxEqual(view[targetLength * hiddenSize - 1], 0.5f));
}

TEST_CASE("preprocessSpeakerEmbedding crossfade between two speakers", "[speakerembedding][realworld]") {
    // Simulate crossfade: speaker1 proportion decreases, speaker2 increases
    int hiddenSize = 2;
    int64_t targetLength = 10;
    double frameWidth = 0.01;

    Co::InputSpeakerInfo spk1;
    spk1.name = "s1";
    spk1.interval = 0.05; // 50ms
    spk1.proportions = {1.0, 0.5, 0.0}; // fade out

    Co::InputSpeakerInfo spk2;
    spk2.name = "s2";
    spk2.interval = 0.05;
    spk2.proportions = {0.0, 0.5, 1.0}; // fade in

    auto speakers = std::vector<Co::InputSpeakerInfo>{spk1, spk2};
    auto embMap = std::map<std::string, std::vector<float>>{
        {"s1", makeEmbedding(hiddenSize, 1.0f)},
        {"s2", makeEmbedding(hiddenSize, 2.0f)}
    };

    auto exp = preprocessSpeakerEmbeddingFrames(speakers, embMap, hiddenSize, frameWidth, targetLength);
    REQUIRE(exp.hasValue());
    auto tensor = exp.take();
    auto view = tensor->view<float>();
    // At any frame, proportion1 + proportion2 ≈ 1.0
    // So value ≈ 1.0*p1*1.0 + 1.0*p2*2.0 = p1 + 2*p2
    // Since p1+p2≈1, value ≈ p1 + 2*(1-p1) = 2 - p1
    // At start (p1=1): value ≈ 1.0
    // At end (p1=0): value ≈ 2.0
    float firstFrame = view[0];
    float lastFrame = view[(targetLength - 1) * hiddenSize];
    REQUIRE(firstFrame < lastFrame); // should increase over time
}

// ---------------------------------------------------------------------------
// BF-34: preprocessSpeakerEmbeddingFrames must not silently skip speakers
// when proportions is empty or when interval is 0 with multiple proportions.
// These cases previously caused resample() to return {} and the speaker was
// skipped without error, violating ROBUST-05.
// ---------------------------------------------------------------------------

TEST_CASE("BF-34 empty proportions returns error", "[speakerembedding][extreme]") {
    int hiddenSize = 4;
    int64_t targetLength = 10;
    double frameWidth = 0.01;

    Co::InputSpeakerInfo spk;
    spk.name = "s1";
    spk.interval = 0.01;
    spk.proportions = {}; // empty proportions

    auto speakers = std::vector<Co::InputSpeakerInfo>{spk};
    auto embMap = std::map<std::string, std::vector<float>>{
        {"s1", makeEmbedding(hiddenSize, 1.0f)}
    };

    auto exp = preprocessSpeakerEmbeddingFrames(speakers, embMap, hiddenSize, frameWidth, targetLength);
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.error().message().find("empty proportions") != std::string::npos);
}

TEST_CASE("BF-34 multiple proportions with zero interval returns error", "[speakerembedding][extreme]") {
    int hiddenSize = 4;
    int64_t targetLength = 10;
    double frameWidth = 0.01;

    Co::InputSpeakerInfo spk;
    spk.name = "s1";
    spk.interval = 0; // zero interval with multiple proportions
    spk.proportions = {0.0, 0.5, 1.0};

    auto speakers = std::vector<Co::InputSpeakerInfo>{spk};
    auto embMap = std::map<std::string, std::vector<float>>{
        {"s1", makeEmbedding(hiddenSize, 1.0f)}
    };

    auto exp = preprocessSpeakerEmbeddingFrames(speakers, embMap, hiddenSize, frameWidth, targetLength);
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.error().message().find("interval is 0") != std::string::npos);
}

TEST_CASE("BF-34 single proportion with zero interval is valid", "[speakerembedding][extreme]") {
    // Single proportion + interval=0 is the "static speaker" pattern.
    // resample() broadcasts the single value to all frames. This must
    // still succeed after the BF-34 fix.
    int hiddenSize = 4;
    int64_t targetLength = 10;
    double frameWidth = 0.01;

    auto speakers = std::vector<Co::InputSpeakerInfo>{makeStaticSpeaker("s1")};
    auto embMap = std::map<std::string, std::vector<float>>{
        {"s1", makeEmbedding(hiddenSize, 1.0f)}
    };

    auto exp = preprocessSpeakerEmbeddingFrames(speakers, embMap, hiddenSize, frameWidth, targetLength);
    REQUIRE(exp.hasValue());
    auto tensor = exp.take();
    auto view = tensor->view<float>();
    for (int64_t i = 0; i < targetLength; ++i) {
        for (int j = 0; j < hiddenSize; ++j) {
            REQUIRE(approxEqual(view[i * hiddenSize + j], 1.0f));
        }
    }
}

TEST_CASE("BF-34 empty proportions among multiple speakers fails fast", "[speakerembedding][extreme]") {
    // First speaker is valid, second has empty proportions.
    // The function must fail on the second speaker, not silently skip it.
    int hiddenSize = 4;
    int64_t targetLength = 5;
    double frameWidth = 0.01;

    Co::InputSpeakerInfo spk1 = makeStaticSpeaker("s1");
    Co::InputSpeakerInfo spk2;
    spk2.name = "s2";
    spk2.interval = 0.01;
    spk2.proportions = {}; // empty

    auto speakers = std::vector<Co::InputSpeakerInfo>{spk1, spk2};
    auto embMap = std::map<std::string, std::vector<float>>{
        {"s1", makeEmbedding(hiddenSize, 1.0f)},
        {"s2", makeEmbedding(hiddenSize, 2.0f)}
    };

    auto exp = preprocessSpeakerEmbeddingFrames(speakers, embMap, hiddenSize, frameWidth, targetLength);
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.error().message().find("s2") != std::string::npos);
    REQUIRE(exp.error().message().find("empty proportions") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Inline embedding tests: allows custom/undefined speakers with direct
// embedding vectors, matching ds-editor-lite's custom voice mix mechanism.
// ---------------------------------------------------------------------------

TEST_CASE("inline embedding for undefined speaker succeeds", "[speakerembedding][inline]") {
    // Speaker not in embMap, but inline embedding provided.
    // This is the primary use case: custom voice mix with arbitrary emb values.
    int hiddenSize = 4;
    int64_t targetLength = 5;
    double frameWidth = 0.01;

    Co::InputSpeakerInfo spk = makeStaticSpeaker("custom_spk");
    spk.embedding = makeEmbedding(hiddenSize, 0.7f); // inline embedding

    auto speakers = std::vector<Co::InputSpeakerInfo>{spk};
    auto embMap = std::map<std::string, std::vector<float>>{}; // empty: no voice bank speakers

    auto exp = preprocessSpeakerEmbeddingFrames(speakers, embMap, hiddenSize, frameWidth, targetLength);
    REQUIRE(exp.hasValue());
    auto tensor = exp.take();
    auto view = tensor->view<float>();
    for (int64_t i = 0; i < targetLength; ++i) {
        for (int j = 0; j < hiddenSize; ++j) {
            REQUIRE(approxEqual(view[i * hiddenSize + j], 0.7f));
        }
    }
}

TEST_CASE("inline embedding with dynamic proportions", "[speakerembedding][inline]") {
    // Custom speaker with time-varying proportions and inline embedding.
    int hiddenSize = 2;
    int64_t targetLength = 10;
    double frameWidth = 0.01;

    Co::InputSpeakerInfo spk;
    spk.name = "custom";
    spk.interval = 0.1; // wide enough interval to cover all 10 frames
    spk.proportions = {0.0, 1.0};
    spk.embedding = makeEmbedding(hiddenSize, 1.0f);

    auto speakers = std::vector<Co::InputSpeakerInfo>{spk};
    auto embMap = std::map<std::string, std::vector<float>>{};

    auto exp = preprocessSpeakerEmbeddingFrames(speakers, embMap, hiddenSize, frameWidth, targetLength);
    REQUIRE(exp.hasValue());
    auto tensor = exp.take();
    auto view = tensor->view<float>();
    // First frame should be 0 (proportion=0), last should be >0.5 (ramp up)
    REQUIRE(view[0] < 0.1f);
    REQUIRE(view[(targetLength - 1) * hiddenSize] > 0.5f);
}

TEST_CASE("voice bank speaker takes priority over inline embedding", "[speakerembedding][inline]") {
    // When both embMap and inline embedding are available, embMap wins.
    // This preserves backward compatibility for defined speakers.
    int hiddenSize = 4;
    int64_t targetLength = 3;
    double frameWidth = 0.01;

    Co::InputSpeakerInfo spk = makeStaticSpeaker("s1");
    spk.embedding = makeEmbedding(hiddenSize, 0.9f); // should be ignored

    auto speakers = std::vector<Co::InputSpeakerInfo>{spk};
    auto embMap = std::map<std::string, std::vector<float>>{
        {"s1", makeEmbedding(hiddenSize, 0.3f)}
    };

    auto exp = preprocessSpeakerEmbeddingFrames(speakers, embMap, hiddenSize, frameWidth, targetLength);
    REQUIRE(exp.hasValue());
    auto tensor = exp.take();
    auto view = tensor->view<float>();
    // Should use embMap value (0.3), not inline (0.9)
    for (int64_t i = 0; i < targetLength; ++i) {
        for (int j = 0; j < hiddenSize; ++j) {
            REQUIRE(approxEqual(view[i * hiddenSize + j], 0.3f));
        }
    }
}

TEST_CASE("undefined speaker without inline embedding returns error", "[speakerembedding][inline]") {
    // Speaker not in embMap and no inline embedding → error (not silent skip)
    int hiddenSize = 4;
    int64_t targetLength = 5;
    double frameWidth = 0.01;

    Co::InputSpeakerInfo spk = makeStaticSpeaker("unknown");

    auto speakers = std::vector<Co::InputSpeakerInfo>{spk};
    auto embMap = std::map<std::string, std::vector<float>>{
        {"s1", makeEmbedding(hiddenSize, 1.0f)}
    };

    auto exp = preprocessSpeakerEmbeddingFrames(speakers, embMap, hiddenSize, frameWidth, targetLength);
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.error().message().find("unknown") != std::string::npos);
    REQUIRE(exp.error().message().find("no inline embedding") != std::string::npos);
}

TEST_CASE("inline embedding size mismatch returns error", "[speakerembedding][inline][extreme]") {
    int hiddenSize = 4;
    int64_t targetLength = 3;
    double frameWidth = 0.01;

    Co::InputSpeakerInfo spk = makeStaticSpeaker("custom");
    spk.embedding = makeEmbedding(3, 0.5f); // wrong size: 3 instead of 4

    auto speakers = std::vector<Co::InputSpeakerInfo>{spk};
    auto embMap = std::map<std::string, std::vector<float>>{};

    auto exp = preprocessSpeakerEmbeddingFrames(speakers, embMap, hiddenSize, frameWidth, targetLength);
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.error().message().find("hiddenSize") != std::string::npos);
}

TEST_CASE("mix of voice bank speaker and inline embedding speaker", "[speakerembedding][inline]") {
    // Real-world: one defined speaker + one custom speaker with inline embedding
    int hiddenSize = 2;
    int64_t targetLength = 5;
    double frameWidth = 0.01;

    Co::InputSpeakerInfo spk1 = makeStaticSpeaker("s1"); // from voice bank
    Co::InputSpeakerInfo spk2 = makeStaticSpeaker("custom"); // not in voice bank
    spk2.embedding = makeEmbedding(hiddenSize, 1.0f);
    spk2.proportions = {0.5}; // half weight

    auto speakers = std::vector<Co::InputSpeakerInfo>{spk1, spk2};
    auto embMap = std::map<std::string, std::vector<float>>{
        {"s1", makeEmbedding(hiddenSize, 1.0f)}
    };

    auto exp = preprocessSpeakerEmbeddingFrames(speakers, embMap, hiddenSize, frameWidth, targetLength);
    REQUIRE(exp.hasValue());
    auto tensor = exp.take();
    auto view = tensor->view<float>();
    // spk1: 1.0 * 1.0 = 1.0, spk2: 0.5 * 1.0 = 0.5, total = 1.5
    for (int64_t i = 0; i < targetLength; ++i) {
        for (int j = 0; j < hiddenSize; ++j) {
            REQUIRE(approxEqual(view[i * hiddenSize + j], 1.5f));
        }
    }
}

TEST_CASE("empty name with inline embedding succeeds", "[speakerembedding][inline][extreme]") {
    // Speaker with empty name but valid inline embedding — should work
    // since embedding is provided directly.
    int hiddenSize = 4;
    int64_t targetLength = 3;
    double frameWidth = 0.01;

    Co::InputSpeakerInfo spk;
    spk.name = ""; // no name
    spk.interval = 0;
    spk.proportions = {1.0};
    spk.embedding = makeEmbedding(hiddenSize, 0.5f);

    auto speakers = std::vector<Co::InputSpeakerInfo>{spk};
    auto embMap = std::map<std::string, std::vector<float>>{};

    auto exp = preprocessSpeakerEmbeddingFrames(speakers, embMap, hiddenSize, frameWidth, targetLength);
    REQUIRE(exp.hasValue());
    auto tensor = exp.take();
    auto view = tensor->view<float>();
    for (int64_t i = 0; i < targetLength; ++i) {
        for (int j = 0; j < hiddenSize; ++j) {
            REQUIRE(approxEqual(view[i * hiddenSize + j], 0.5f));
        }
    }
}
