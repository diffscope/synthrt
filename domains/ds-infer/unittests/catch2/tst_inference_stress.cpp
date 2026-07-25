// tst_inference_stress.cpp
// T-06 推理流水线超大输入压力测试 (P2, BF-46 回归)。
//
// 在 inferutil 预处理层（preprocessPhoneme* / preprocessSpeakerEmbedding*）
// 验证超大输入下的健壮性：不崩溃、内存合理、错误路径明确。L1 路径直接
// 调用 inferutil 函数，无需 ONNX Runtime / 插件 DLL。
//
// 现有 tst_input_word_extreme.cpp / tst_speaker_embedding_extreme.cpp 已覆盖：
//   - BF-41 frameWidth 校验（NaN/Inf/0/负/subnormal）
//   - 空 words 列表（preprocessPhoneme* 三件套）
//   - 100 words × 1 phone = 1000 frames
//   - 60s 单音符 → 6000 frames
//   - BF-42 speaker embedding frameWidth 校验
//   - 2-3 speaker FixedMix / DynamicMix
//
// 本文件补充以下超大输入场景（T-06 spec 要求）：
//   1. 10000 phoneme 输入 → 不崩溃，targetLength 合理
//   2. 空 notes 序列（word.notes=[]）→ 明确错误或空结果
//   3. 超长单音符（3600s = 1 小时）→ 时长分配不溢出
//   4. 极端 speaker mix（100+ speaker）→ 不崩溃
//
// 准则核对：ROBUST-03（防空）；BF-46（超大输入防御）。
// 验收：4 用例；无崩溃/OOM。

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <synthrt/Core/Support/Error.h>

#include <inferutil/InputWord.h>
#include <inferutil/SpeakerEmbedding.h>

#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>

using namespace ds::infer::inferutil;
using srt::core::ErrorCode;
namespace Co = srt::svs::Api::Common::L1;

namespace {

    // 构建单 word，含 N 个 phone 共享一个 note（单音符多音素场景）。
    Co::InputWordInfo makeMultiPhoneWord(int phoneCount, double noteDuration) {
        Co::InputWordInfo word;
        word.phones.reserve(static_cast<size_t>(phoneCount));
        const double phoneStartStep = noteDuration / phoneCount;
        for (int i = 0; i < phoneCount; ++i) {
            Co::InputPhonemeInfo phone;
            phone.token = "p" + std::to_string(i);
            phone.language = "op";
            phone.tone = 0;
            phone.start = static_cast<double>(i) * phoneStartStep;
            phone.speakers.push_back({"default", 1.0});
            word.phones.push_back(std::move(phone));
        }
        Co::InputNoteInfo note;
        note.key = 60;
        note.duration = noteDuration;
        word.notes.push_back(note);
        return word;
    }

    // 构建 N 个 word，每个 word 含 M 个 phone + 1 个 note。
    std::vector<Co::InputWordInfo> makeWords(int wordCount, int phonesPerWord,
                                              double noteDuration) {
        std::vector<Co::InputWordInfo> words;
        words.reserve(static_cast<size_t>(wordCount));
        double timeOffset = 0.0;
        for (int i = 0; i < wordCount; ++i) {
            auto word = makeMultiPhoneWord(phonesPerWord, noteDuration);
            // 时间偏移：每个 word 的 phone.start 累加前一个 word 的总时长。
            for (auto &phone : word.phones) {
                phone.start += timeOffset;
            }
            words.push_back(std::move(word));
            timeOffset += noteDuration;
        }
        return words;
    }

    // 构建空 notes 的 word（合法结构但无音符）。
    Co::InputWordInfo makeWordWithEmptyNotes() {
        Co::InputWordInfo word;
        Co::InputPhonemeInfo phone;
        phone.token = "a";
        phone.language = "op";
        phone.start = 0.0;
        phone.speakers.push_back({"default", 1.0});
        word.phones.push_back(std::move(phone));
        // word.notes 留空
        return word;
    }

    std::vector<float> makeEmbedding(int hiddenSize, float fillValue) {
        return std::vector<float>(static_cast<size_t>(hiddenSize), fillValue);
    }

    // FixedMix 模式：N 个 speaker，每 speaker proportions={weight}。
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

} // namespace

// ===========================================================================
// 1. 10000 phoneme 输入 → 不崩溃，targetLength 合理
//
// 现有 tst_input_word_extreme.cpp "many words aggregated targetLength" 仅测试
// 100 words × 1 phone = 100 phones。本用例推到 10000 phones：
//   - 1000 words × 10 phones/word = 10000 phones
//   - 每 word 0.5s → 总时长 500s → frameWidth=0.01 → 50000 frames
// 验证：
//   - 不崩溃、不 OOM
//   - targetLength == 50000（500s / 0.01s）
//   - tensor elementCount == 10000（phone 数）
// ===========================================================================

TEST_CASE("10000 phoneme input does not crash and produces correct targetLength",
          "[inference][stress][phonemes][scale]") {
    // 1000 words × 10 phones = 10000 phones，每 word 0.5s
    const int wordCount = 1000;
    const int phonesPerWord = 10;
    const double noteDuration = 0.5;
    auto words = makeWords(wordCount, phonesPerWord, noteDuration);
    REQUIRE(getPhoneCount(words) == 10000);

    int64_t targetLength = -1;
    const double frameWidth = 0.01;  // 10ms
    auto exp = preprocessPhonemeDurations(words, frameWidth, &targetLength);
    REQUIRE(exp.hasValue());
    auto tensor = exp.take();
    REQUIRE(tensor);
    // tensor 形状：[phoneCount]，每个 phone 一个 duration 值
    REQUIRE(tensor->elementCount() == 10000);
    // 总时长 500s / 0.01s = 50000 frames
    REQUIRE(targetLength == 50000);
}

TEST_CASE("10000 phoneme tokens preprocessing does not crash",
          "[inference][stress][phonemes][scale]") {
    // 同样 10000 phones，但测试 preprocessPhonemeTokens 路径
    const int wordCount = 1000;
    const int phonesPerWord = 10;
    const double noteDuration = 0.5;
    auto words = makeWords(wordCount, phonesPerWord, noteDuration);
    REQUIRE(getPhoneCount(words) == 10000);

    // 构建足够大的 token 表，每个 phone token 映射到唯一 id
    std::map<std::string, int> tokens;
    for (int w = 0; w < wordCount; ++w) {
        for (int p = 0; p < phonesPerWord; ++p) {
            const std::string tok = "op/p" + std::to_string(p);
            tokens[tok] = p + 1;
        }
    }

    auto exp = preprocessPhonemeTokens(words, tokens);
    REQUIRE(exp.hasValue());
    auto tensor = exp.take();
    REQUIRE(tensor);
    REQUIRE(tensor->elementCount() == 10000);
}

TEST_CASE("10000 phoneme languages preprocessing does not crash",
          "[inference][stress][phonemes][scale]") {
    const int wordCount = 1000;
    const int phonesPerWord = 10;
    const double noteDuration = 0.5;
    auto words = makeWords(wordCount, phonesPerWord, noteDuration);
    REQUIRE(getPhoneCount(words) == 10000);

    std::map<std::string, int> languages{{"op", 1}};

    auto exp = preprocessPhonemeLanguages(words, languages);
    REQUIRE(exp.hasValue());
    auto tensor = exp.take();
    REQUIRE(tensor);
    REQUIRE(tensor->elementCount() == 10000);
}

// ===========================================================================
// 2. 空 notes 序列 → 明确错误或空结果
//
// 现有 tst_input_word_extreme.cpp 测试空 words 列表（words.size()==0），
// 但未测试 word.notes 为空的非空 word。preprocessPhonemeDurations 依赖
// note.duration 求和得到 word 时长，空 notes 会导致 word 时长为 0。
// 验证：
//   - 不崩溃
//   - 要么返回错误（明确拒绝），要么返回 targetLength=0 + 空 tensor
// ===========================================================================

TEST_CASE("Empty notes within word does not crash",
          "[inference][stress][empty-notes]") {
    auto word = makeWordWithEmptyNotes();
    std::vector<Co::InputWordInfo> words{word};

    int64_t targetLength = -1;
    const double frameWidth = 0.01;
    auto exp = preprocessPhonemeDurations(words, frameWidth, &targetLength);

    // 接受两种合法行为：
    //   (a) 返回错误（明确拒绝空 notes）
    //   (b) 返回成功 + targetLength=0 + 空/零 tensor
    if (exp.hasValue()) {
        // 成功路径：targetLength 必须 >= 0（不能是初始值 -1）
        REQUIRE(targetLength >= 0);
        auto tensor = exp.take();
        if (tensor) {
            // tensor 可以是空（0 元素）或包含 1 个 phone 的 0 duration
            REQUIRE(tensor->elementCount() <= 1);
        }
    } else {
        // 错误路径：必须返回结构化错误，不能崩溃
        REQUIRE(!exp.hasValue());
        // 错误码可以是 InvalidArgument 或 SessionError，只要不崩溃
    }
}

TEST_CASE("Multiple words with empty notes does not crash",
          "[inference][stress][empty-notes]") {
    // 多个 word 都有空 notes
    std::vector<Co::InputWordInfo> words;
    for (int i = 0; i < 5; ++i) {
        words.push_back(makeWordWithEmptyNotes());
    }

    int64_t targetLength = -1;
    const double frameWidth = 0.01;
    auto exp = preprocessPhonemeDurations(words, frameWidth, &targetLength);

    // 不崩溃即可；接受成功或错误路径
    if (exp.hasValue()) {
        REQUIRE(targetLength >= 0);
    } else {
        REQUIRE(!exp.hasValue());
    }
}

// ===========================================================================
// 3. 超长单音符（3600s = 1 小时）→ 时长分配不溢出
//
// 现有 tst_input_word_extreme.cpp "long duration produces large frame count"
// 测试 60s → 6000 frames。本用例推到 3600s（1 小时）：
//   - frameWidth=0.01 → 360000 frames
//   - 验证 targetLength == 360000（int64_t 不溢出）
//   - 验证 tensor elementCount == 1（单 phone 单 note）
//
// 这是 ds-editor-lite 渲染超长持续音（如 drone、pad 音色）的极端场景。
// ===========================================================================

TEST_CASE("Ultra-long single note (3600s) does not overflow targetLength",
          "[inference][stress][long-note][overflow]") {
    const double noteDuration = 3600.0;  // 1 小时
    auto word = makeMultiPhoneWord(1, noteDuration);
    std::vector<Co::InputWordInfo> words{word};

    int64_t targetLength = -1;
    const double frameWidth = 0.01;  // 10ms
    auto exp = preprocessPhonemeDurations(words, frameWidth, &targetLength);
    REQUIRE(exp.hasValue());
    auto tensor = exp.take();
    REQUIRE(tensor);
    REQUIRE(tensor->elementCount() == 1);
    // 3600s / 0.01s = 360000 frames（int64_t 远未溢出）
    REQUIRE(targetLength == 360000);
}

TEST_CASE("Ultra-long single note with small frameWidth does not overflow",
          "[inference][stress][long-note][overflow]") {
    // 3600s + 1ms frameWidth = 3,600,000 frames（仍远低于 int64_t 上限）
    const double noteDuration = 3600.0;
    auto word = makeMultiPhoneWord(1, noteDuration);
    std::vector<Co::InputWordInfo> words{word};

    int64_t targetLength = -1;
    const double frameWidth = 0.001;  // 1ms
    auto exp = preprocessPhonemeDurations(words, frameWidth, &targetLength);
    REQUIRE(exp.hasValue());
    REQUIRE(targetLength == 3600000);
}

// ===========================================================================
// 4. 极端 speaker mix（100+ speaker）→ 不崩溃
//
// 现有 tst_speaker_embedding_custom_mix.cpp 测试 2-3 speaker FixedMix /
// DynamicMix。本用例推到 100 speaker：
//   - 100 speaker FixedMix，每 speaker 权重 1/100 = 0.01
//   - 验证 mix 后总和近 1.0
//   - 验证不崩溃、不 OOM
//
// 这是 ds-editor-lite 多角色合唱场景的极端输入。
// ===========================================================================

TEST_CASE("100-speaker FixedMix does not crash and sums to ~1.0",
          "[inference][stress][speaker-mix][scale]") {
    const int speakerCount = 100;
    const int hiddenSize = 4;
    const int64_t targetLength = 100;
    const double frameWidth = 0.01;

    // 100 speaker，每 speaker 权重 1/100
    std::vector<std::string> names;
    names.reserve(speakerCount);
    std::vector<double> weights;
    weights.reserve(speakerCount);
    std::map<std::string, std::vector<float>> embMap;
    for (int i = 0; i < speakerCount; ++i) {
        const std::string name = "s" + std::to_string(i);
        names.push_back(name);
        weights.push_back(1.0 / speakerCount);
        // 每 speaker 的 embedding 用不同 fillValue 以便验证 mix 结果
        embMap[name] = makeEmbedding(hiddenSize, static_cast<float>(i));
    }

    auto speakers = fixedMixSpeakers(names, weights);
    auto exp = preprocessSpeakerEmbeddingFrames(speakers, embMap, hiddenSize,
                                                 frameWidth, targetLength);
    REQUIRE(exp.hasValue());
    auto tensor = exp.take();
    REQUIRE(tensor);
    // 输出大小：targetLength × hiddenSize
    REQUIRE(tensor->elementCount() == static_cast<size_t>(targetLength * hiddenSize));

    auto view = tensor->view<float>();
    // mix 后每帧每维应等于所有 speaker embedding 的加权和。
    // 由于每 speaker 的 fillValue = i，权重 = 1/100，
    // 期望值 = sum(i * 1/100 for i in 0..99) = 99*100/2 / 100 = 49.5
    const float expected = static_cast<float>(99.0 * 100.0 / 2.0 / 100.0);
    for (int64_t frame = 0; frame < targetLength; ++frame) {
        for (int h = 0; h < hiddenSize; ++h) {
            const float actual = view[static_cast<size_t>(frame * hiddenSize + h)];
            // 允许累加误差（100 个 float 累加，误差 < 1e-3）
            REQUIRE(std::abs(actual - expected) < 1e-2f);
        }
    }
}

TEST_CASE("200-speaker FixedMix does not crash",
          "[inference][stress][speaker-mix][scale]") {
    // 推到 200 speaker，验证不崩溃（不验证精确 mix 值，避免浮点累加误差）
    const int speakerCount = 200;
    const int hiddenSize = 4;
    const int64_t targetLength = 10;
    const double frameWidth = 0.01;

    std::vector<std::string> names;
    names.reserve(speakerCount);
    std::vector<double> weights;
    weights.reserve(speakerCount);
    std::map<std::string, std::vector<float>> embMap;
    for (int i = 0; i < speakerCount; ++i) {
        const std::string name = "s" + std::to_string(i);
        names.push_back(name);
        weights.push_back(1.0 / speakerCount);
        embMap[name] = makeEmbedding(hiddenSize, 1.0f);
    }

    auto speakers = fixedMixSpeakers(names, weights);
    auto exp = preprocessSpeakerEmbeddingFrames(speakers, embMap, hiddenSize,
                                                 frameWidth, targetLength);
    REQUIRE(exp.hasValue());
    auto tensor = exp.take();
    REQUIRE(tensor);
    REQUIRE(tensor->elementCount() == static_cast<size_t>(targetLength * hiddenSize));

    // 所有权重 = 1/200，所有 embedding fillValue = 1.0 → mix 结果 = 1.0
    auto view = tensor->view<float>();
    for (size_t i = 0; i < view.size(); ++i) {
        REQUIRE(std::abs(view[i] - 1.0f) < 1e-4f);
    }
}

TEST_CASE("100-speaker DynamicMix does not crash",
          "[inference][stress][speaker-mix][scale]") {
    // 100 speaker DynamicMix：每 speaker proportions 是时间序列，
    // frame i 时 speaker (i % 100) 权重 1.0，其余 0.0。
    // 注意：精确 mix 值验证由 tst_speaker_embedding_custom_mix.cpp 覆盖，
    // 本用例仅验证 100-speaker 规模下不崩溃、输出大小正确。
    const int speakerCount = 100;
    const int hiddenSize = 4;
    const int64_t targetLength = 100;
    const double frameWidth = 0.01;

    std::vector<Co::InputSpeakerInfo> speakers;
    speakers.reserve(speakerCount);
    std::map<std::string, std::vector<float>> embMap;
    for (int s = 0; s < speakerCount; ++s) {
        Co::InputSpeakerInfo spk;
        spk.name = "s" + std::to_string(s);
        spk.interval = frameWidth;
        // 该 speaker 在 frame == s 时权重 1.0，其余 frame 权重 0.0
        spk.proportions.assign(static_cast<size_t>(targetLength), 0.0);
        spk.proportions[static_cast<size_t>(s)] = 1.0;
        speakers.push_back(std::move(spk));
        embMap["s" + std::to_string(s)] = makeEmbedding(hiddenSize, static_cast<float>(s));
    }

    auto exp = preprocessSpeakerEmbeddingFrames(speakers, embMap, hiddenSize,
                                                 frameWidth, targetLength);
    REQUIRE(exp.hasValue());
    auto tensor = exp.take();
    REQUIRE(tensor);
    // 输出大小：targetLength × hiddenSize
    REQUIRE(tensor->elementCount() == static_cast<size_t>(targetLength * hiddenSize));

    // 验证所有值为有限浮点数（mix 结果应基于输入 embedding 的加权和，
    // 不应产生 NaN/Inf）。精确值验证由 custom_mix 测试覆盖。
    auto view = tensor->view<float>();
    for (size_t i = 0; i < view.size(); ++i) {
        REQUIRE(std::isfinite(view[i]));
    }
}
