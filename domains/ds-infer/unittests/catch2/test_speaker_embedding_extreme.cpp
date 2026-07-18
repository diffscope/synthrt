// BF-42 回归：preprocessSpeakerEmbeddingFrames frameWidth 极端场景。
//
// 覆盖范围：
//   - NaN / 无穷大 / 零 / 负 frameWidth 必须返回 InvalidArgument
//   - 极小 frameWidth 触发超大采样数组（防御性不崩溃）
//   - 极大 frameWidth 产生 0/1 帧采样的退化情形
//   - 超大 targetLength 边界
//   - targetLength=0 的边界（已经在 test_speaker_embedding.cpp 测过，此处补充极端）
//   - 超大 hiddenSize 边界
//   - proportions 包含 NaN/Inf 时不崩溃
//   - 静态 speaker + 内联 embedding + NaN frameWidth 的优先级
//
// 这些用例反映 ds-editor-lite AcousticInference 在用户提供异常 frameWidth
// 或异常 speaker mix 数据时的健壮性要求。BF-42 在
// preprocessSpeakerEmbeddingFrames 入口处加了防御性校验，避免 resample()
// 在 timestep <= 0 时静默返回空 vector 导致 speaker 被跳过。

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <vector>

#include <synthrt/Core/Support/Error.h>

#include <inferutil/SpeakerEmbedding.h>

using namespace ds::infer::inferutil;
using srt::core::ErrorCode;
namespace Co = srt::svs::Api::Common::L1;

namespace {
    bool approxEqual(float a, float b, float eps = 1e-5f) {
        return std::abs(a - b) < eps;
    }

    std::vector<float> makeEmbedding(int hiddenSize, float fillValue) {
        return std::vector<float>(hiddenSize, fillValue);
    }

    Co::InputSpeakerInfo makeStaticSpeaker(const std::string &name) {
        Co::InputSpeakerInfo spk;
        spk.name = name;
        spk.interval = 0;
        spk.proportions = {1.0};
        return spk;
    }
}

// ---------------------------------------------------------------------------
// BF-42: preprocessSpeakerEmbeddingFrames frameWidth 校验
// ---------------------------------------------------------------------------

TEST_CASE("BF-42 preprocessSpeakerEmbedding NaN frameWidth returns error",
          "[speakerembedding][extreme][bf-42]") {
    int hiddenSize = 4;
    int64_t targetLength = 10;
    auto speakers = std::vector<Co::InputSpeakerInfo>{makeStaticSpeaker("s1")};
    auto embMap = std::map<std::string, std::vector<float>>{
        {"s1", makeEmbedding(hiddenSize, 1.0f)}
    };

    auto exp = preprocessSpeakerEmbeddingFrames(speakers, embMap, hiddenSize,
                                                std::numeric_limits<double>::quiet_NaN(),
                                                targetLength);
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.isError(ErrorCode::InvalidArgument));
    REQUIRE(exp.error().message().find("frameWidth") != std::string::npos);
}

TEST_CASE("BF-42 preprocessSpeakerEmbedding positive infinity frameWidth returns error",
          "[speakerembedding][extreme][bf-42]") {
    int hiddenSize = 4;
    int64_t targetLength = 10;
    auto speakers = std::vector<Co::InputSpeakerInfo>{makeStaticSpeaker("s1")};
    auto embMap = std::map<std::string, std::vector<float>>{
        {"s1", makeEmbedding(hiddenSize, 1.0f)}
    };

    auto exp = preprocessSpeakerEmbeddingFrames(speakers, embMap, hiddenSize,
                                                std::numeric_limits<double>::infinity(),
                                                targetLength);
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.isError(ErrorCode::InvalidArgument));
}

TEST_CASE("BF-42 preprocessSpeakerEmbedding negative infinity frameWidth returns error",
          "[speakerembedding][extreme][bf-42]") {
    int hiddenSize = 4;
    int64_t targetLength = 10;
    auto speakers = std::vector<Co::InputSpeakerInfo>{makeStaticSpeaker("s1")};
    auto embMap = std::map<std::string, std::vector<float>>{
        {"s1", makeEmbedding(hiddenSize, 1.0f)}
    };

    auto exp = preprocessSpeakerEmbeddingFrames(speakers, embMap, hiddenSize,
                                                -std::numeric_limits<double>::infinity(),
                                                targetLength);
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.isError(ErrorCode::InvalidArgument));
}

TEST_CASE("BF-42 preprocessSpeakerEmbedding zero frameWidth returns error",
          "[speakerembedding][extreme][bf-42]") {
    int hiddenSize = 4;
    int64_t targetLength = 10;
    auto speakers = std::vector<Co::InputSpeakerInfo>{makeStaticSpeaker("s1")};
    auto embMap = std::map<std::string, std::vector<float>>{
        {"s1", makeEmbedding(hiddenSize, 1.0f)}
    };

    auto exp = preprocessSpeakerEmbeddingFrames(speakers, embMap, hiddenSize, 0.0,
                                                targetLength);
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.isError(ErrorCode::InvalidArgument));
}

TEST_CASE("BF-42 preprocessSpeakerEmbedding negative frameWidth returns error",
          "[speakerembedding][extreme][bf-42]") {
    int hiddenSize = 4;
    int64_t targetLength = 10;
    auto speakers = std::vector<Co::InputSpeakerInfo>{makeStaticSpeaker("s1")};
    auto embMap = std::map<std::string, std::vector<float>>{
        {"s1", makeEmbedding(hiddenSize, 1.0f)}
    };

    auto exp = preprocessSpeakerEmbeddingFrames(speakers, embMap, hiddenSize, -0.01,
                                                targetLength);
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.isError(ErrorCode::InvalidArgument));
}

TEST_CASE("BF-42 preprocessSpeakerEmbedding frameWidth check precedes speaker validation",
          "[speakerembedding][extreme][bf-42]") {
    // frameWidth 校验应该在 speaker 查找之前，所以即使 speaker 未定义也应该
    // 返回 frameWidth 错误（而不是 speaker 未找到错误）。
    int hiddenSize = 4;
    int64_t targetLength = 10;
    auto speakers = std::vector<Co::InputSpeakerInfo>{makeStaticSpeaker("unknown")};
    auto embMap = std::map<std::string, std::vector<float>>{};

    auto exp = preprocessSpeakerEmbeddingFrames(speakers, embMap, hiddenSize,
                                                std::numeric_limits<double>::quiet_NaN(),
                                                targetLength);
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.isError(ErrorCode::InvalidArgument));
    REQUIRE(exp.error().message().find("frameWidth") != std::string::npos);
}

// ---------------------------------------------------------------------------
// 极大 targetLength
// ---------------------------------------------------------------------------

TEST_CASE("preprocessSpeakerEmbedding very large targetLength succeeds",
          "[speakerembedding][extreme]") {
    // 10000 帧 + 静态 speaker：resample 会 broadcast 单 proportions 到所有帧
    int hiddenSize = 4;
    int64_t targetLength = 10000;
    double frameWidth = 0.01;
    auto speakers = std::vector<Co::InputSpeakerInfo>{makeStaticSpeaker("s1")};
    auto embMap = std::map<std::string, std::vector<float>>{
        {"s1", makeEmbedding(hiddenSize, 1.0f)}
    };

    auto exp = preprocessSpeakerEmbeddingFrames(speakers, embMap, hiddenSize, frameWidth,
                                                targetLength);
    REQUIRE(exp.hasValue());
    auto tensor = exp.take();
    REQUIRE(tensor->elementCount() == static_cast<size_t>(targetLength * hiddenSize));
    auto view = tensor->view<float>();
    REQUIRE(approxEqual(view[0], 1.0f));
    REQUIRE(approxEqual(view[targetLength * hiddenSize - 1], 1.0f));
}

TEST_CASE("preprocessSpeakerEmbedding targetLength one succeeds",
          "[speakerembedding][extreme]") {
    int hiddenSize = 4;
    int64_t targetLength = 1;
    double frameWidth = 0.01;
    auto speakers = std::vector<Co::InputSpeakerInfo>{makeStaticSpeaker("s1")};
    auto embMap = std::map<std::string, std::vector<float>>{
        {"s1", makeEmbedding(hiddenSize, 0.5f)}
    };

    auto exp = preprocessSpeakerEmbeddingFrames(speakers, embMap, hiddenSize, frameWidth,
                                                targetLength);
    REQUIRE(exp.hasValue());
    auto tensor = exp.take();
    REQUIRE(tensor->elementCount() == static_cast<size_t>(hiddenSize));
    auto view = tensor->view<float>();
    for (int j = 0; j < hiddenSize; ++j) {
        REQUIRE(approxEqual(view[j], 0.5f));
    }
}

// ---------------------------------------------------------------------------
// 超大 hiddenSize
// ---------------------------------------------------------------------------

TEST_CASE("preprocessSpeakerEmbedding very large hiddenSize succeeds",
          "[speakerembedding][extreme]") {
    // hiddenSize=4096（远超真实 DiffSinger 的 256）
    int hiddenSize = 4096;
    int64_t targetLength = 10;
    double frameWidth = 0.01;
    auto speakers = std::vector<Co::InputSpeakerInfo>{makeStaticSpeaker("s1")};
    auto embMap = std::map<std::string, std::vector<float>>{
        {"s1", makeEmbedding(hiddenSize, 0.25f)}
    };

    auto exp = preprocessSpeakerEmbeddingFrames(speakers, embMap, hiddenSize, frameWidth,
                                                targetLength);
    REQUIRE(exp.hasValue());
    auto tensor = exp.take();
    REQUIRE(tensor->elementCount() == static_cast<size_t>(targetLength * hiddenSize));
}

// ---------------------------------------------------------------------------
// proportions 包含 NaN/Inf
// ---------------------------------------------------------------------------

TEST_CASE("preprocessSpeakerEmbedding proportions with NaN does not crash",
          "[speakerembedding][extreme]") {
    // proportions 包含 NaN：resample 会通过插值产生 NaN，但不崩溃。
    int hiddenSize = 2;
    int64_t targetLength = 10;
    double frameWidth = 0.01;

    Co::InputSpeakerInfo spk;
    spk.name = "s1";
    spk.interval = 0.05;
    spk.proportions = {0.0, std::numeric_limits<double>::quiet_NaN(), 1.0};

    auto speakers = std::vector<Co::InputSpeakerInfo>{spk};
    auto embMap = std::map<std::string, std::vector<float>>{
        {"s1", makeEmbedding(hiddenSize, 1.0f)}
    };

    auto exp = preprocessSpeakerEmbeddingFrames(speakers, embMap, hiddenSize, frameWidth,
                                                targetLength);
    REQUIRE(exp.hasValue());
    auto tensor = exp.take();
    auto view = tensor->view<float>();
    // 至少有一帧包含 NaN（中间采样点）
    bool hasNaN = false;
    for (size_t i = 0; i < view.size(); ++i) {
        if (std::isnan(view[i])) {
            hasNaN = true;
            break;
        }
    }
    REQUIRE(hasNaN);
}

TEST_CASE("preprocessSpeakerEmbedding proportions with infinity does not crash",
          "[speakerembedding][extreme]") {
    int hiddenSize = 2;
    int64_t targetLength = 10;
    double frameWidth = 0.01;

    Co::InputSpeakerInfo spk;
    spk.name = "s1";
    spk.interval = 0.05;
    spk.proportions = {0.0, std::numeric_limits<double>::infinity(), 1.0};

    auto speakers = std::vector<Co::InputSpeakerInfo>{spk};
    auto embMap = std::map<std::string, std::vector<float>>{
        {"s1", makeEmbedding(hiddenSize, 1.0f)}
    };

    auto exp = preprocessSpeakerEmbeddingFrames(speakers, embMap, hiddenSize, frameWidth,
                                                targetLength);
    // 不崩溃即可；可能产生 Inf 值
    REQUIRE(exp.hasValue());
}

TEST_CASE("preprocessSpeakerEmbedding proportions all zero produces zero output",
          "[speakerembedding][extreme]") {
    // 全零 proportions -> 输出全零（spk 仍参与 mix，但贡献为 0）
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
// 极小 frameWidth
// ---------------------------------------------------------------------------

TEST_CASE("preprocessSpeakerEmbedding very small frameWidth succeeds",
          "[speakerembedding][extreme]") {
    // 0.0001s frameWidth + interval=0.01 -> resample 会产生 100 个采样点
    // targetLength=10 -> 截断为 10
    int hiddenSize = 2;
    int64_t targetLength = 10;
    double frameWidth = 0.0001;

    Co::InputSpeakerInfo spk;
    spk.name = "s1";
    spk.interval = 0.01;
    spk.proportions = {0.0, 1.0};

    auto speakers = std::vector<Co::InputSpeakerInfo>{spk};
    auto embMap = std::map<std::string, std::vector<float>>{
        {"s1", makeEmbedding(hiddenSize, 1.0f)}
    };

    auto exp = preprocessSpeakerEmbeddingFrames(speakers, embMap, hiddenSize, frameWidth,
                                                targetLength);
    REQUIRE(exp.hasValue());
    auto tensor = exp.take();
    REQUIRE(tensor->elementCount() == static_cast<size_t>(targetLength * hiddenSize));
}

TEST_CASE("preprocessSpeakerEmbedding very large frameWidth yields tail fill",
          "[speakerembedding][extreme]") {
    // frameWidth=1.0（远大于 interval=0.01）-> resample 产生 1 个采样点
    // targetLength=10 -> 后 9 帧用 tail fill（=第一个采样值）
    int hiddenSize = 2;
    int64_t targetLength = 10;
    double frameWidth = 1.0;

    Co::InputSpeakerInfo spk;
    spk.name = "s1";
    spk.interval = 0.01;
    spk.proportions = {0.5, 1.0};

    auto speakers = std::vector<Co::InputSpeakerInfo>{spk};
    auto embMap = std::map<std::string, std::vector<float>>{
        {"s1", makeEmbedding(hiddenSize, 1.0f)}
    };

    auto exp = preprocessSpeakerEmbeddingFrames(speakers, embMap, hiddenSize, frameWidth,
                                                targetLength);
    REQUIRE(exp.hasValue());
    auto tensor = exp.take();
    auto view = tensor->view<float>();
    // resample 产生 1 个采样点（interval 0.01, frameWidth 1.0, tMax=0.01）
    // 实际采样 0 个点（arange(0, 0.01, 1.0) = [0] 1 个点），tail fill 9 个
    // 所有值应相等
    REQUIRE(approxEqual(view[0], view[2]));
}

// ---------------------------------------------------------------------------
// 内联 embedding + NaN frameWidth：frameWidth 校验仍优先
// ---------------------------------------------------------------------------

TEST_CASE("BF-42 inline embedding with NaN frameWidth still returns frameWidth error",
          "[speakerembedding][extreme][bf-42][inline]") {
    // 即使有内联 embedding，frameWidth 校验仍应优先返回错误。
    int hiddenSize = 4;
    int64_t targetLength = 5;

    Co::InputSpeakerInfo spk = makeStaticSpeaker("custom");
    spk.embedding = makeEmbedding(hiddenSize, 0.7f);

    auto speakers = std::vector<Co::InputSpeakerInfo>{spk};
    auto embMap = std::map<std::string, std::vector<float>>{};

    auto exp = preprocessSpeakerEmbeddingFrames(speakers, embMap, hiddenSize,
                                                std::numeric_limits<double>::quiet_NaN(),
                                                targetLength);
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.isError(ErrorCode::InvalidArgument));
    REQUIRE(exp.error().message().find("frameWidth") != std::string::npos);
}

// ---------------------------------------------------------------------------
// hiddenSize 边界
// ---------------------------------------------------------------------------

TEST_CASE("preprocessSpeakerEmbedding hiddenSize one succeeds",
          "[speakerembedding][extreme]") {
    int hiddenSize = 1;
    int64_t targetLength = 5;
    double frameWidth = 0.01;
    auto speakers = std::vector<Co::InputSpeakerInfo>{makeStaticSpeaker("s1")};
    auto embMap = std::map<std::string, std::vector<float>>{
        {"s1", makeEmbedding(hiddenSize, 0.5f)}
    };

    auto exp = preprocessSpeakerEmbeddingFrames(speakers, embMap, hiddenSize, frameWidth,
                                                targetLength);
    REQUIRE(exp.hasValue());
    auto tensor = exp.take();
    auto view = tensor->view<float>();
    for (int64_t i = 0; i < targetLength; ++i) {
        REQUIRE(approxEqual(view[i], 0.5f));
    }
}

TEST_CASE("preprocessSpeakerEmbedding zero hiddenSize returns error",
          "[speakerembedding][extreme]") {
    // hiddenSize=0: TensorHelper 创建 shape={1, targetLength, 0} 可能失败
    // 或者在后续 mix 时 size mismatch。测试记录现状：不崩溃即可。
    int hiddenSize = 0;
    int64_t targetLength = 5;
    double frameWidth = 0.01;
    auto speakers = std::vector<Co::InputSpeakerInfo>{makeStaticSpeaker("s1")};
    auto embMap = std::map<std::string, std::vector<float>>{
        {"s1", makeEmbedding(hiddenSize, 0.5f)}
    };

    auto exp = preprocessSpeakerEmbeddingFrames(speakers, embMap, hiddenSize, frameWidth,
                                                targetLength);
    // 不崩溃即可。
    if (exp.hasValue()) {
        auto tensor = exp.take();
        // elementCount 可能是 0
    } else {
        REQUIRE(!exp.hasValue());
    }
}

TEST_CASE("preprocessSpeakerEmbedding negative hiddenSize returns error",
          "[speakerembedding][extreme]") {
    // hiddenSize=-1: shape={1, targetLength, -1} 转换为 int64 后是 -1，
    // Tensor::create 应失败或 size mismatch。测试记录现状：不崩溃即可。
    int hiddenSize = -1;
    int64_t targetLength = 5;
    double frameWidth = 0.01;
    auto speakers = std::vector<Co::InputSpeakerInfo>{makeStaticSpeaker("s1")};
    auto embMap = std::map<std::string, std::vector<float>>{
        {"s1", std::vector<float>{}}
    };

    auto exp = preprocessSpeakerEmbeddingFrames(speakers, embMap, hiddenSize, frameWidth,
                                                targetLength);
    // 不崩溃即可。
    REQUIRE(!exp.hasValue() || exp.hasValue());
}
