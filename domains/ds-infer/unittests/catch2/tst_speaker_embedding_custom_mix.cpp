// 自定义 voice mix 测试：覆盖 ds-editor-lite 的 Single/FixedMix/DynamicMix 模式。
//
// 覆盖范围：
//   - Single 模式：单个 speaker，proportions={1.0}，interval=0（静态）
//   - FixedMix 模式：N 个 speaker，每 speaker proportions={weight}（N-1 显式权重
//     + 1 个 1-sum 兜底，对应 ds-editor-lite 的 fullWeightsFromExplicitWeights）
//   - DynamicMix 模式：N 个 speaker，每 speaker proportions 是时间序列（keyframe
//     插值后的 N-1 权重存储 + 兜底 speaker 的 1-sum 帧）
//   - crossfade：两个 speaker proportions 互补（p1+p2≈1）
//   - 三 speaker mix：N=3 时 N-1 显式 + 兜底
//   - 内联 embedding 用于自定义 voice（不依赖 voice bank）
//   - mix 后总和近 1.0 的不变式
//
// 这些用例反映 ds-editor-lite InferSpeakerMixModel::staticSpeakerMix /
// fixedSpeakerMixFromData / dynamicSpeakerMixFromData 的实际调用模式。
// 注意：ds-editor-lite 在 ds-infer 之外计算 fullWeights，这里直接构造
// InputSpeakerInfo 来模拟最终进入 preprocessSpeakerEmbeddingFrames 的数据。

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

    std::vector<float> makeEmbedding(int hiddenSize, float fillValue) {
        return std::vector<float>(hiddenSize, fillValue);
    }

    // Single 模式：单 speaker 全权重
    Co::InputSpeakerInfo singleSpeaker(const std::string &name) {
        Co::InputSpeakerInfo spk;
        spk.name = name;
        spk.interval = 0;
        spk.proportions = {1.0};
        return spk;
    }

    // FixedMix 模式：N 个 speaker 静态权重（每 speaker proportions={weight}）
    std::vector<Co::InputSpeakerInfo>
        fixedMixSpeakers(const std::vector<std::string> &names,
                          const std::vector<double> &weights) {
        std::vector<Co::InputSpeakerInfo> speakers;
        speakers.reserve(names.size());
        for (size_t i = 0; i < names.size(); ++i) {
            Co::InputSpeakerInfo spk;
            spk.name = names[i];
            spk.interval = 0;
            spk.proportions = {weights[i]};
            speakers.push_back(std::move(spk));
        }
        return speakers;
    }

    // DynamicMix 模式：N 个 speaker 时间序列权重（每 speaker proportions 是 vector）
    // interval = intervalSeconds，proportions.size() = frames
    std::vector<Co::InputSpeakerInfo>
        dynamicMixSpeakers(const std::vector<std::string> &names,
                            const std::vector<std::vector<double>> &weightsPerFrame,
                            double intervalSeconds) {
        std::vector<Co::InputSpeakerInfo> speakers;
        speakers.reserve(names.size());
        for (size_t i = 0; i < names.size(); ++i) {
            Co::InputSpeakerInfo spk;
            spk.name = names[i];
            spk.interval = intervalSeconds;
            spk.proportions = weightsPerFrame[i];
            speakers.push_back(std::move(spk));
        }
        return speakers;
    }
}

// ---------------------------------------------------------------------------
// Single 模式
// ---------------------------------------------------------------------------

TEST_CASE("custom voice mix: Single mode produces pure speaker output",
          "[speakerembedding][custommix][single]") {
    // Single 模式：1 个 speaker 全权重 -> 输出 = 1.0 * embedding
    int hiddenSize = 4;
    int64_t targetLength = 10;
    double frameWidth = 0.01;

    auto speakers = std::vector<Co::InputSpeakerInfo>{singleSpeaker("s1")};
    auto embMap = std::map<std::string, std::vector<float>>{
        {"s1", makeEmbedding(hiddenSize, 0.5f)}
    };

    auto exp = preprocessSpeakerEmbeddingFrames(speakers, embMap, hiddenSize, frameWidth,
                                                targetLength);
    REQUIRE(exp.hasValue());
    auto tensor = exp.take();
    auto view = tensor->view<float>();
    for (int64_t i = 0; i < targetLength; ++i) {
        for (int j = 0; j < hiddenSize; ++j) {
            REQUIRE(approxEqual(view[i * hiddenSize + j], 0.5f));
        }
    }
}

TEST_CASE("custom voice mix: Single mode with zero weight produces zero output",
          "[speakerembedding][custommix][single]") {
    // Single 模式但权重为 0 -> 输出全零
    int hiddenSize = 4;
    int64_t targetLength = 5;
    double frameWidth = 0.01;

    Co::InputSpeakerInfo spk;
    spk.name = "s1";
    spk.interval = 0;
    spk.proportions = {0.0};

    auto speakers = std::vector<Co::InputSpeakerInfo>{spk};
    auto embMap = std::map<std::string, std::vector<float>>{
        {"s1", makeEmbedding(hiddenSize, 1.0f)}
    };

    auto exp = preprocessSpeakerEmbeddingFrames(speakers, embMap, hiddenSize, frameWidth,
                                                targetLength);
    REQUIRE(exp.hasValue());
    auto tensor = exp.take();
    auto view = tensor->view<float>();
    for (size_t i = 0; i < view.size(); ++i) {
        REQUIRE(approxEqual(view[i], 0.0f));
    }
}

// ---------------------------------------------------------------------------
// FixedMix 模式（N 个静态权重，权重和 ≈ 1）
// ---------------------------------------------------------------------------

TEST_CASE("custom voice mix: FixedMix two speakers weight sum one",
          "[speakerembedding][custommix][fixedmix]") {
    // FixedMix：2 speaker，权重 0.3 + 0.7 = 1.0
    int hiddenSize = 2;
    int64_t targetLength = 5;
    double frameWidth = 0.01;

    auto speakers = fixedMixSpeakers({"s1", "s2"}, {0.3, 0.7});
    auto embMap = std::map<std::string, std::vector<float>>{
        {"s1", makeEmbedding(hiddenSize, 1.0f)},
        {"s2", makeEmbedding(hiddenSize, 2.0f)}
    };

    auto exp = preprocessSpeakerEmbeddingFrames(speakers, embMap, hiddenSize, frameWidth,
                                                targetLength);
    REQUIRE(exp.hasValue());
    auto tensor = exp.take();
    auto view = tensor->view<float>();
    // 每帧 = 0.3*1.0 + 0.7*2.0 = 0.3 + 1.4 = 1.7
    for (int64_t i = 0; i < targetLength; ++i) {
        for (int j = 0; j < hiddenSize; ++j) {
            REQUIRE(approxEqual(view[i * hiddenSize + j], 1.7f));
        }
    }
}

TEST_CASE("custom voice mix: FixedMix three speakers weight sum one",
          "[speakerembedding][custommix][fixedmix]") {
    // FixedMix：3 speaker，权重 0.2 + 0.3 + 0.5 = 1.0（N-1 显式 + 1 兜底）
    int hiddenSize = 1;
    int64_t targetLength = 3;
    double frameWidth = 0.01;

    auto speakers = fixedMixSpeakers({"s1", "s2", "s3"}, {0.2, 0.3, 0.5});
    auto embMap = std::map<std::string, std::vector<float>>{
        {"s1", makeEmbedding(hiddenSize, 1.0f)},
        {"s2", makeEmbedding(hiddenSize, 2.0f)},
        {"s3", makeEmbedding(hiddenSize, 3.0f)}
    };

    auto exp = preprocessSpeakerEmbeddingFrames(speakers, embMap, hiddenSize, frameWidth,
                                                targetLength);
    REQUIRE(exp.hasValue());
    auto tensor = exp.take();
    auto view = tensor->view<float>();
    // 每帧 = 0.2*1 + 0.3*2 + 0.5*3 = 0.2 + 0.6 + 1.5 = 2.3
    for (int64_t i = 0; i < targetLength; ++i) {
        REQUIRE(approxEqual(view[i], 2.3f));
    }
}

TEST_CASE("custom voice mix: FixedMix weight sum not one still mixes linearly",
          "[speakerembedding][custommix][fixedmix]") {
    // FixedMix：权重和 != 1（ds-editor-lite 不强制归一化），mix 仍线性组合
    int hiddenSize = 2;
    int64_t targetLength = 5;
    double frameWidth = 0.01;

    auto speakers = fixedMixSpeakers({"s1", "s2"}, {0.5, 0.5});
    auto embMap = std::map<std::string, std::vector<float>>{
        {"s1", makeEmbedding(hiddenSize, 1.0f)},
        {"s2", makeEmbedding(hiddenSize, 1.0f)}
    };

    auto exp = preprocessSpeakerEmbeddingFrames(speakers, embMap, hiddenSize, frameWidth,
                                                targetLength);
    REQUIRE(exp.hasValue());
    auto tensor = exp.take();
    auto view = tensor->view<float>();
    // 每帧 = 0.5*1 + 0.5*1 = 1.0
    for (int64_t i = 0; i < targetLength; ++i) {
        for (int j = 0; j < hiddenSize; ++j) {
            REQUIRE(approxEqual(view[i * hiddenSize + j], 1.0f));
        }
    }
}

// ---------------------------------------------------------------------------
// DynamicMix 模式（N 个时间序列权重）
// ---------------------------------------------------------------------------

TEST_CASE("custom voice mix: DynamicMix two speakers crossfade",
          "[speakerembedding][custommix][dynamicmix]") {
    // DynamicMix：2 speaker 时间序列，p1 从 1.0 降到 0.0，p2 从 0.0 升到 1.0
    // 5 帧，interval=0.05s（每帧 50ms），targetLength=5, frameWidth=0.01
    int hiddenSize = 1;
    int64_t targetLength = 5;
    double frameWidth = 0.01;

    // 每帧权重（5 帧）
    std::vector<double> p1Frames = {1.0, 0.75, 0.5, 0.25, 0.0};
    std::vector<double> p2Frames = {0.0, 0.25, 0.5, 0.75, 1.0};

    auto speakers = dynamicMixSpeakers({"s1", "s2"}, {p1Frames, p2Frames}, 0.05);
    auto embMap = std::map<std::string, std::vector<float>>{
        {"s1", makeEmbedding(hiddenSize, 1.0f)},
        {"s2", makeEmbedding(hiddenSize, 2.0f)}
    };

    auto exp = preprocessSpeakerEmbeddingFrames(speakers, embMap, hiddenSize, frameWidth,
                                                targetLength);
    REQUIRE(exp.hasValue());
    auto tensor = exp.take();
    auto view = tensor->view<float>();
    // resample 从 interval=0.05 重采样到 frameWidth=0.01，前 5 帧对应时间
    // [0, 0.01, 0.02, 0.03, 0.04]，在 inputTimeAxis [0, 0.05, 0.10, 0.15, 0.20]
    // 上线性插值。p1 插值结果 = [1.0, 0.95, 0.90, 0.85, 0.80]，
    // p2 = 1-p1 = [0.0, 0.05, 0.10, 0.15, 0.20]。
    // view[i] = p1[i]*1 + p2[i]*2 = 2 - p1[i]:
    //   view[0] = 2 - 1.0 = 1.0
    //   view[4] = 2 - 0.80 = 1.20
    REQUIRE(approxEqual(view[0], 1.0f));
    REQUIRE(approxEqual(view[4], 1.2f));
    // 单调递增
    REQUIRE(view[0] < view[2]);
    REQUIRE(view[2] < view[4]);
}

TEST_CASE("custom voice mix: DynamicMix three speakers weighted",
          "[speakerembedding][custommix][dynamicmix]") {
    // DynamicMix：3 speaker，每帧 3 个权重
    int hiddenSize = 1;
    int64_t targetLength = 4;
    double frameWidth = 0.01;

    std::vector<double> p1 = {0.5, 0.4, 0.3, 0.2};
    std::vector<double> p2 = {0.3, 0.3, 0.3, 0.3};
    std::vector<double> p3 = {0.2, 0.3, 0.4, 0.5};

    auto speakers = dynamicMixSpeakers({"s1", "s2", "s3"}, {p1, p2, p3}, 0.04);
    auto embMap = std::map<std::string, std::vector<float>>{
        {"s1", makeEmbedding(hiddenSize, 1.0f)},
        {"s2", makeEmbedding(hiddenSize, 2.0f)},
        {"s3", makeEmbedding(hiddenSize, 3.0f)}
    };

    auto exp = preprocessSpeakerEmbeddingFrames(speakers, embMap, hiddenSize, frameWidth,
                                                targetLength);
    REQUIRE(exp.hasValue());
    auto tensor = exp.take();
    auto view = tensor->view<float>();
    // resample 从 interval=0.04 重采样到 frameWidth=0.01，前 4 帧对应时间
    // [0, 0.01, 0.02, 0.03]，全部落在第一个源区间 [0, 0.04] 内。
    // p1 插值结果 = [0.5, 0.475, 0.45, 0.425]
    // p2 恒为 0.3
    // p3 插值结果 = [0.2, 0.225, 0.25, 0.275]
    // 第 0 帧：0.5*1 + 0.3*2 + 0.2*3 = 0.5 + 0.6 + 0.6 = 1.7
    REQUIRE(approxEqual(view[0], 1.7f));
    // 第 3 帧：0.425*1 + 0.3*2 + 0.275*3 = 0.425 + 0.6 + 0.825 = 1.85
    REQUIRE(approxEqual(view[3], 1.85f));
}

TEST_CASE("custom voice mix: DynamicMix single frame proportion broadcasts",
          "[speakerembedding][custommix][dynamicmix]") {
    // DynamicMix 但 proportions 只有 1 帧 -> 等价于 FixedMix
    int hiddenSize = 2;
    int64_t targetLength = 10;
    double frameWidth = 0.01;

    std::vector<double> p1 = {0.4};
    std::vector<double> p2 = {0.6};

    auto speakers = dynamicMixSpeakers({"s1", "s2"}, {p1, p2}, 0.01);
    auto embMap = std::map<std::string, std::vector<float>>{
        {"s1", makeEmbedding(hiddenSize, 1.0f)},
        {"s2", makeEmbedding(hiddenSize, 1.0f)}
    };

    auto exp = preprocessSpeakerEmbeddingFrames(speakers, embMap, hiddenSize, frameWidth,
                                                targetLength);
    REQUIRE(exp.hasValue());
    auto tensor = exp.take();
    auto view = tensor->view<float>();
    // 所有帧：0.4*1 + 0.6*1 = 1.0
    for (int64_t i = 0; i < targetLength; ++i) {
        for (int j = 0; j < hiddenSize; ++j) {
            REQUIRE(approxEqual(view[i * hiddenSize + j], 1.0f));
        }
    }
}

// ---------------------------------------------------------------------------
// 内联 embedding + Custom Mix（自定义 voice 不在 voice bank 中）
// ---------------------------------------------------------------------------

TEST_CASE("custom voice mix: inline embedding two custom speakers mix",
          "[speakerembedding][custommix][inline]") {
    // 两个自定义 speaker，都用内联 embedding，权重 0.5/0.5
    int hiddenSize = 2;
    int64_t targetLength = 5;
    double frameWidth = 0.01;

    Co::InputSpeakerInfo spk1;
    spk1.name = "custom1";
    spk1.interval = 0;
    spk1.proportions = {0.5};
    spk1.embedding = makeEmbedding(hiddenSize, 1.0f);

    Co::InputSpeakerInfo spk2;
    spk2.name = "custom2";
    spk2.interval = 0;
    spk2.proportions = {0.5};
    spk2.embedding = makeEmbedding(hiddenSize, 3.0f);

    auto speakers = std::vector<Co::InputSpeakerInfo>{spk1, spk2};
    auto embMap = std::map<std::string, std::vector<float>>{}; // 空 voice bank

    auto exp = preprocessSpeakerEmbeddingFrames(speakers, embMap, hiddenSize, frameWidth,
                                                targetLength);
    REQUIRE(exp.hasValue());
    auto tensor = exp.take();
    auto view = tensor->view<float>();
    // 每帧 = 0.5*1 + 0.5*3 = 2.0
    for (int64_t i = 0; i < targetLength; ++i) {
        for (int j = 0; j < hiddenSize; ++j) {
            REQUIRE(approxEqual(view[i * hiddenSize + j], 2.0f));
        }
    }
}

TEST_CASE("custom voice mix: mix of voice bank and inline embedding speakers",
          "[speakerembedding][custommix][inline]") {
    // 一个 voice bank speaker + 一个自定义 inline speaker
    int hiddenSize = 2;
    int64_t targetLength = 3;
    double frameWidth = 0.01;

    Co::InputSpeakerInfo spk1 = singleSpeaker("bank1"); // voice bank
    Co::InputSpeakerInfo spk2 = singleSpeaker("custom1"); // inline
    spk2.embedding = makeEmbedding(hiddenSize, 0.5f);
    spk2.proportions = {0.5};

    auto speakers = std::vector<Co::InputSpeakerInfo>{spk1, spk2};
    auto embMap = std::map<std::string, std::vector<float>>{
        {"bank1", makeEmbedding(hiddenSize, 1.0f)}
    };

    auto exp = preprocessSpeakerEmbeddingFrames(speakers, embMap, hiddenSize, frameWidth,
                                                targetLength);
    REQUIRE(exp.hasValue());
    auto tensor = exp.take();
    auto view = tensor->view<float>();
    // 每帧 = 1.0*1 + 0.5*0.5 = 1.25
    for (int64_t i = 0; i < targetLength; ++i) {
        for (int j = 0; j < hiddenSize; ++j) {
            REQUIRE(approxEqual(view[i * hiddenSize + j], 1.25f));
        }
    }
}

// ---------------------------------------------------------------------------
// N-1 权重存储 + 兜底 speaker 模拟（ds-editor-lite 实际数据流）
// ---------------------------------------------------------------------------

TEST_CASE("custom voice mix: N-1 explicit weights with fallback speaker",
          "[speakerembedding][custommix][fallback]") {
    // ds-editor-lite 实际存储 N-1 个显式权重，第 N 个 = 1 - sum(N-1)
    // 这里模拟 3 speaker 场景：显式存储 s1=0.4, s2=0.3，s3=1-0.4-0.3=0.3
    int hiddenSize = 1;
    int64_t targetLength = 5;
    double frameWidth = 0.01;

    auto speakers = fixedMixSpeakers({"s1", "s2", "s3"}, {0.4, 0.3, 0.3});
    auto embMap = std::map<std::string, std::vector<float>>{
        {"s1", makeEmbedding(hiddenSize, 1.0f)},
        {"s2", makeEmbedding(hiddenSize, 2.0f)},
        {"s3", makeEmbedding(hiddenSize, 3.0f)}
    };

    auto exp = preprocessSpeakerEmbeddingFrames(speakers, embMap, hiddenSize, frameWidth,
                                                targetLength);
    REQUIRE(exp.hasValue());
    auto tensor = exp.take();
    auto view = tensor->view<float>();
    // 每帧 = 0.4*1 + 0.3*2 + 0.3*3 = 0.4 + 0.6 + 0.9 = 1.9
    for (int64_t i = 0; i < targetLength; ++i) {
        REQUIRE(approxEqual(view[i], 1.9f));
    }
}

TEST_CASE("custom voice mix: DynamicMix N-1 explicit weights with fallback per frame",
          "[speakerembedding][custommix][fallback][dynamicmix]") {
    // DynamicMix 模式下，每帧 N-1 个显式 + 兜底
    // s1, s2 显式，s3 = 1 - s1 - s2
    int hiddenSize = 1;
    int64_t targetLength = 3;
    double frameWidth = 0.01;

    std::vector<double> s1 = {0.2, 0.5, 0.8};
    std::vector<double> s2 = {0.3, 0.3, 0.1};
    std::vector<double> s3;
    s3.reserve(s1.size());
    for (size_t i = 0; i < s1.size(); ++i) {
        s3.push_back(1.0 - s1[i] - s2[i]); // 0.5, 0.2, 0.1
    }

    auto speakers = dynamicMixSpeakers({"s1", "s2", "s3"}, {s1, s2, s3}, 0.03);
    auto embMap = std::map<std::string, std::vector<float>>{
        {"s1", makeEmbedding(hiddenSize, 1.0f)},
        {"s2", makeEmbedding(hiddenSize, 2.0f)},
        {"s3", makeEmbedding(hiddenSize, 3.0f)}
    };

    auto exp = preprocessSpeakerEmbeddingFrames(speakers, embMap, hiddenSize, frameWidth,
                                                targetLength);
    REQUIRE(exp.hasValue());
    auto tensor = exp.take();
    auto view = tensor->view<float>();
    // resample 从 interval=0.03 重采样到 frameWidth=0.01，前 3 帧对应时间
    // [0, 0.01, 0.02]，全部落在第一个源区间 [0, 0.03] 内。
    // s1 插值结果 = [0.2, 0.3, 0.4] (0.2 + t/0.03 * (0.5-0.2))
    // s2 恒为 0.3
    // s3 插值结果 = [0.5, 0.4, 0.3] (0.5 + t/0.03 * (0.2-0.5))
    // 第 0 帧：0.2*1 + 0.3*2 + 0.5*3 = 0.2 + 0.6 + 1.5 = 2.3
    REQUIRE(approxEqual(view[0], 2.3f));
    // 第 2 帧：0.4*1 + 0.3*2 + 0.3*3 = 0.4 + 0.6 + 0.9 = 1.9
    REQUIRE(approxEqual(view[2], 1.9f));
}

// ---------------------------------------------------------------------------
// Crossfade 不变式：p1+p2 ≈ 1 时 mix 输出在 [emb1, emb2] 之间单调
// ---------------------------------------------------------------------------

TEST_CASE("custom voice mix: crossfade preserves monotonic interpolation",
          "[speakerembedding][custommix][crossfade]") {
    int hiddenSize = 1;
    int64_t targetLength = 11; // 11 帧
    double frameWidth = 0.01;

    // 11 帧的 crossfade 权重
    std::vector<double> p1, p2;
    p1.reserve(11);
    p2.reserve(11);
    for (int i = 0; i < 11; ++i) {
        double v = static_cast<double>(i) / 10.0; // 0.0 -> 1.0
        p1.push_back(1.0 - v);
        p2.push_back(v);
    }

    auto speakers = dynamicMixSpeakers({"s1", "s2"}, {p1, p2}, 0.1);
    auto embMap = std::map<std::string, std::vector<float>>{
        {"s1", makeEmbedding(hiddenSize, 0.0f)},
        {"s2", makeEmbedding(hiddenSize, 10.0f)}
    };

    auto exp = preprocessSpeakerEmbeddingFrames(speakers, embMap, hiddenSize, frameWidth,
                                                targetLength);
    REQUIRE(exp.hasValue());
    auto tensor = exp.take();
    auto view = tensor->view<float>();
    // resample 从 interval=0.1 重采样到 frameWidth=0.01，前 11 帧对应时间
    // [0, 0.01, 0.02, ..., 0.10]，全部落在第一个源区间 [0, 0.1] 内。
    // 第 0 帧 (t=0.00)：p1=1.0, p2=0.0 -> 1.0*0 + 0.0*10 = 0
    REQUIRE(approxEqual(view[0], 0.0f));
    // 第 10 帧 (t=0.10)：恰好命中 inputTimeAxis[1]=0.1，
    // p1=0.9, p2=0.1 -> 0.9*0 + 0.1*10 = 1.0
    REQUIRE(approxEqual(view[10], 1.0f));
    // 中间帧应单调递增
    for (int i = 1; i < 11; ++i) {
        REQUIRE(view[i] >= view[i - 1] - 1e-3f);
    }
}
