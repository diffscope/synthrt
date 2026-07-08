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
