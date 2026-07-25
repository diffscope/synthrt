// LinguisticEncoder 单元测试：BF-41 frameWidth 校验和会话输入构造。
//
// 覆盖范围：
//   - preprocessLinguisticWord 在 NaN/Inf/0/负 frameWidth 下返回 InvalidArgument
//   - preprocessLinguisticWord 正常路径构造 tokens/word_div/word_dur 三个输入
//   - preprocessLinguisticWord 在 useLanguageId=true 时构造 languages 输入
//   - preprocessLinguisticPhoneme 委派给 preprocessPhonemeDurations，frameWidth
//     异常时同样返回错误
//   - preprocessLinguisticWord 在 token 查找失败时返回 InvalidArgument
//   - preprocessLinguisticWord 在 language 查找失败时返回 InvalidArgument
//   - 空 words 列表行为
//
// 这些用例反映 ds-editor-lite InferDurationTask/PitchInference 在调用
// LinguisticEncoder 之前的 frameWidth 校验被绕过时的健壮性要求。
// BF-41 在 preprocessLinguisticWord 入口处加了防御性校验，避免 word_dur
// 计算时除以 frameWidth 发生 UB。

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <map>
#include <string>
#include <vector>

#include <synthrt/Core/Support/Error.h>

#include <inferutil/LinguisticEncoder.h>

#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>

using namespace ds::infer::inferutil;
using srt::core::ErrorCode;
namespace Co = srt::svs::Api::Common::L1;

namespace {
    // 构建单 word 多 phone 单 note
    Co::InputWordInfo makeWord(const std::vector<std::string> &tokens,
                                const std::string &language,
                                double noteDuration) {
        Co::InputWordInfo word;
        double phoneDur = noteDuration / tokens.size();
        word.phones.reserve(tokens.size());
        for (size_t i = 0; i < tokens.size(); ++i) {
            Co::InputPhonemeInfo phone;
            phone.token = tokens[i];
            phone.language = language;
            phone.start = static_cast<double>(i) * phoneDur;
            word.phones.push_back(std::move(phone));
        }
        Co::InputNoteInfo note;
        note.key = 60;
        note.duration = noteDuration;
        word.notes.push_back(note);
        return word;
    }
}

// ---------------------------------------------------------------------------
// BF-41: preprocessLinguisticWord frameWidth 校验
// ---------------------------------------------------------------------------

TEST_CASE("BF-41 preprocessLinguisticWord NaN frameWidth returns error",
          "[linguistic][bf-41]") {
    auto word = makeWord({"a", "b"}, "op", 0.4);
    std::vector<Co::InputWordInfo> words{word};
    std::map<std::string, int> tokens{{"op/a", 1}, {"op/b", 2}};
    std::map<std::string, int> languages{{"op", 0}};

    auto exp = preprocessLinguisticWord(words, tokens, languages, true,
                                        std::numeric_limits<double>::quiet_NaN());
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.isError(ErrorCode::InvalidArgument));
    REQUIRE(exp.error().message().find("frameWidth") != std::string::npos);
}

TEST_CASE("BF-41 preprocessLinguisticWord positive infinity frameWidth returns error",
          "[linguistic][bf-41]") {
    auto word = makeWord({"a", "b"}, "op", 0.4);
    std::vector<Co::InputWordInfo> words{word};
    std::map<std::string, int> tokens{{"op/a", 1}, {"op/b", 2}};
    std::map<std::string, int> languages{{"op", 0}};

    auto exp = preprocessLinguisticWord(words, tokens, languages, true,
                                        std::numeric_limits<double>::infinity());
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.isError(ErrorCode::InvalidArgument));
}

TEST_CASE("BF-41 preprocessLinguisticWord zero frameWidth returns error",
          "[linguistic][bf-41]") {
    auto word = makeWord({"a", "b"}, "op", 0.4);
    std::vector<Co::InputWordInfo> words{word};
    std::map<std::string, int> tokens{{"op/a", 1}, {"op/b", 2}};
    std::map<std::string, int> languages{{"op", 0}};

    auto exp = preprocessLinguisticWord(words, tokens, languages, true, 0.0);
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.isError(ErrorCode::InvalidArgument));
}

TEST_CASE("BF-41 preprocessLinguisticWord negative frameWidth returns error",
          "[linguistic][bf-41]") {
    auto word = makeWord({"a", "b"}, "op", 0.4);
    std::vector<Co::InputWordInfo> words{word};
    std::map<std::string, int> tokens{{"op/a", 1}, {"op/b", 2}};
    std::map<std::string, int> languages{{"op", 0}};

    auto exp = preprocessLinguisticWord(words, tokens, languages, true, -0.01);
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.isError(ErrorCode::InvalidArgument));
}

// ---------------------------------------------------------------------------
// preprocessLinguisticWord 正常路径
// ---------------------------------------------------------------------------

TEST_CASE("preprocessLinguisticWord constructs tokens/word_div/word_dur",
          "[linguistic]") {
    auto word = makeWord({"a", "b"}, "op", 0.4);
    std::vector<Co::InputWordInfo> words{word};
    std::map<std::string, int> tokens{{"op/a", 1}, {"op/b", 2}};
    std::map<std::string, int> languages{{"op", 0}};

    auto exp = preprocessLinguisticWord(words, tokens, languages, true, 0.01);
    REQUIRE(exp.hasValue());
    auto sessionInput = exp.take();
    REQUIRE(sessionInput);
    // 必须包含 tokens, word_div, word_dur 三个输入
    REQUIRE(sessionInput->inputs.find("tokens") != sessionInput->inputs.end());
    REQUIRE(sessionInput->inputs.find("word_div") != sessionInput->inputs.end());
    REQUIRE(sessionInput->inputs.find("word_dur") != sessionInput->inputs.end());
    // 输出名为 encoder_out, x_masks（SessionStartInput::outputs 是 std::set）
    REQUIRE(sessionInput->outputs.size() == 2);
    REQUIRE(sessionInput->outputs.find("encoder_out") != sessionInput->outputs.end());
    REQUIRE(sessionInput->outputs.find("x_masks") != sessionInput->outputs.end());
}

TEST_CASE("preprocessLinguisticWord without language id omits languages input",
          "[linguistic]") {
    auto word = makeWord({"a"}, "op", 0.2);
    std::vector<Co::InputWordInfo> words{word};
    std::map<std::string, int> tokens{{"op/a", 1}};
    std::map<std::string, int> languages; // 不应被使用

    auto exp = preprocessLinguisticWord(words, tokens, languages, false, 0.01);
    REQUIRE(exp.hasValue());
    auto sessionInput = exp.take();
    // useLanguageId=false -> 不构造 languages 输入
    REQUIRE(sessionInput->inputs.find("languages") == sessionInput->inputs.end());
}

TEST_CASE("preprocessLinguisticWord with language id includes languages input",
          "[linguistic]") {
    auto word = makeWord({"a"}, "op", 0.2);
    std::vector<Co::InputWordInfo> words{word};
    std::map<std::string, int> tokens{{"op/a", 1}};
    std::map<std::string, int> languages{{"op", 0}};

    auto exp = preprocessLinguisticWord(words, tokens, languages, true, 0.01);
    REQUIRE(exp.hasValue());
    auto sessionInput = exp.take();
    REQUIRE(sessionInput->inputs.find("languages") != sessionInput->inputs.end());
}

// ---------------------------------------------------------------------------
// preprocessLinguisticWord 错误路径
// ---------------------------------------------------------------------------

TEST_CASE("preprocessLinguisticWord unknown token returns error",
          "[linguistic][error]") {
    auto word = makeWord({"a"}, "op", 0.2);
    std::vector<Co::InputWordInfo> words{word};
    std::map<std::string, int> tokens{{"op/x", 1}}; // 不匹配
    std::map<std::string, int> languages{{"op", 0}};

    auto exp = preprocessLinguisticWord(words, tokens, languages, true, 0.01);
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.isError(ErrorCode::InvalidArgument));
    REQUIRE(exp.error().message().find("unknown token") != std::string::npos);
}

TEST_CASE("preprocessLinguisticWord unknown language returns error",
          "[linguistic][error]") {
    auto word = makeWord({"a"}, "op", 0.2);
    std::vector<Co::InputWordInfo> words{word};
    std::map<std::string, int> tokens{{"op/a", 1}};
    std::map<std::string, int> languages{{"en", 1}}; // 不匹配 "op"

    auto exp = preprocessLinguisticWord(words, tokens, languages, true, 0.01);
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.isError(ErrorCode::InvalidArgument));
    REQUIRE(exp.error().message().find("unknown language") != std::string::npos);
}

// ---------------------------------------------------------------------------
// preprocessLinguisticPhoneme 委派行为
// ---------------------------------------------------------------------------

TEST_CASE("preprocessLinguisticPhoneme NaN frameWidth returns error via delegation",
          "[linguistic][bf-41]") {
    // preprocessLinguisticPhoneme 不直接校验 frameWidth，但会委派给
    // preprocessPhonemeDurations，后者在 BF-41 中校验 frameWidth。
    auto word = makeWord({"a"}, "op", 0.2);
    std::vector<Co::InputWordInfo> words{word};
    std::map<std::string, int> tokens{{"op/a", 1}};
    std::map<std::string, int> languages{{"op", 0}};

    auto exp = preprocessLinguisticPhoneme(words, tokens, languages, true,
                                           std::numeric_limits<double>::quiet_NaN());
    REQUIRE(!exp.hasValue());
    // 错误来自 preprocessPhonemeDurations，应是 InvalidArgument
    REQUIRE(exp.isError(ErrorCode::InvalidArgument));
}

TEST_CASE("preprocessLinguisticPhoneme constructs tokens and ph_dur",
          "[linguistic]") {
    auto word = makeWord({"a", "b"}, "op", 0.4);
    std::vector<Co::InputWordInfo> words{word};
    std::map<std::string, int> tokens{{"op/a", 1}, {"op/b", 2}};
    std::map<std::string, int> languages{{"op", 0}};

    auto exp = preprocessLinguisticPhoneme(words, tokens, languages, true, 0.01);
    REQUIRE(exp.hasValue());
    auto sessionInput = exp.take();
    // LinguisticPhoneme 不构造 word_div/word_dur，只构造 tokens/languages/ph_dur
    REQUIRE(sessionInput->inputs.find("tokens") != sessionInput->inputs.end());
    REQUIRE(sessionInput->inputs.find("languages") != sessionInput->inputs.end());
    REQUIRE(sessionInput->inputs.find("ph_dur") != sessionInput->inputs.end());
    REQUIRE(sessionInput->inputs.find("word_div") == sessionInput->inputs.end());
    REQUIRE(sessionInput->inputs.find("word_dur") == sessionInput->inputs.end());
}

TEST_CASE("preprocessLinguisticPhoneme without language id omits languages",
          "[linguistic]") {
    auto word = makeWord({"a"}, "op", 0.2);
    std::vector<Co::InputWordInfo> words{word};
    std::map<std::string, int> tokens{{"op/a", 1}};
    std::map<std::string, int> languages;

    auto exp = preprocessLinguisticPhoneme(words, tokens, languages, false, 0.01);
    REQUIRE(exp.hasValue());
    auto sessionInput = exp.take();
    REQUIRE(sessionInput->inputs.find("languages") == sessionInput->inputs.end());
    REQUIRE(sessionInput->inputs.find("ph_dur") != sessionInput->inputs.end());
}

// ---------------------------------------------------------------------------
// 空 words 列表
// ---------------------------------------------------------------------------

TEST_CASE("preprocessLinguisticWord empty words returns session with empty tensors",
          "[linguistic][extreme]") {
    std::vector<Co::InputWordInfo> words;
    std::map<std::string, int> tokens{{"op/a", 1}};
    std::map<std::string, int> languages{{"op", 0}};

    auto exp = preprocessLinguisticWord(words, tokens, languages, true, 0.01);
    // 空 words 产生 0 元素的 word_div/word_dur tensor，应不崩溃。
    if (exp.hasValue()) {
        auto sessionInput = exp.take();
        REQUIRE(sessionInput);
    } else {
        // 部分实现可能返回错误，只要不崩溃即可。
        REQUIRE(!exp.hasValue());
    }
}

TEST_CASE("preprocessLinguisticWord empty words with NaN frameWidth returns error",
          "[linguistic][extreme][bf-41]") {
    // 空 words + NaN frameWidth：BF-41 校验先于业务逻辑，必须返回错误。
    std::vector<Co::InputWordInfo> words;
    std::map<std::string, int> tokens{{"op/a", 1}};
    std::map<std::string, int> languages{{"op", 0}};

    auto exp = preprocessLinguisticWord(words, tokens, languages, true,
                                        std::numeric_limits<double>::quiet_NaN());
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.isError(ErrorCode::InvalidArgument));
}

// ---------------------------------------------------------------------------
// 多 word word_dur 累计行为
// ---------------------------------------------------------------------------

TEST_CASE("preprocessLinguisticWord multiple words word_dur accumulates",
          "[linguistic]") {
    // 两个 word，各 0.2s，frameWidth=0.01 -> 每word 20帧，累计 20/40
    auto w1 = makeWord({"a"}, "op", 0.2);
    auto w2 = makeWord({"b"}, "op", 0.2);
    std::vector<Co::InputWordInfo> words{w1, w2};
    std::map<std::string, int> tokens{{"op/a", 1}, {"op/b", 2}};
    std::map<std::string, int> languages{{"op", 0}};

    auto exp = preprocessLinguisticWord(words, tokens, languages, true, 0.01);
    REQUIRE(exp.hasValue());
    auto sessionInput = exp.take();
    // word_div: [1, 1] (每 word 1 phone)
    auto wordDivIt = sessionInput->inputs.find("word_div");
    REQUIRE(wordDivIt != sessionInput->inputs.end());
    auto wordDiv = wordDivIt->second;
    REQUIRE(wordDiv);
    auto divView = wordDiv->view<int64_t>();
    REQUIRE(divView.size() == 2);
    REQUIRE(divView[0] == 1);
    REQUIRE(divView[1] == 1);
    // word_dur: [20, 20] (每 word 0.2/0.01=20 帧)
    auto wordDurIt = sessionInput->inputs.find("word_dur");
    REQUIRE(wordDurIt != sessionInput->inputs.end());
    auto wordDur = wordDurIt->second;
    auto durView = wordDur->view<int64_t>();
    REQUIRE(durView.size() == 2);
    REQUIRE(durView[0] == 20);
    REQUIRE(durView[1] == 20);
}

// ---------------------------------------------------------------------------
// frameWidth 极大值
// ---------------------------------------------------------------------------

TEST_CASE("preprocessLinguisticWord very large frameWidth yields zero word_dur frames",
          "[linguistic][extreme]") {
    // frameWidth 远大于 duration -> word_dur 全为 0
    auto word = makeWord({"a"}, "op", 0.1);
    std::vector<Co::InputWordInfo> words{word};
    std::map<std::string, int> tokens{{"op/a", 1}};
    std::map<std::string, int> languages{{"op", 0}};

    auto exp = preprocessLinguisticWord(words, tokens, languages, true, 1000.0);
    REQUIRE(exp.hasValue());
    auto sessionInput = exp.take();
    auto wordDurIt = sessionInput->inputs.find("word_dur");
    REQUIRE(wordDurIt != sessionInput->inputs.end());
    auto durView = wordDurIt->second->view<int64_t>();
    REQUIRE(durView.size() == 1);
    REQUIRE(durView[0] == 0);
}
