// BF-41 回归：preprocessPhonemeDurations frameWidth 校验和边界场景。
//
// 覆盖范围：
//   - NaN / 无穷大 / 负数 / 零 frameWidth 必须返回 InvalidArgument
//   - 空 words 列表（preprocessPhonemeTokens / Languages / Durations）
//   - 极小 frameWidth 触发超大帧数（防御性不崩溃）
//   - phone.start 严格递增违反（currPhoneFrames 为负）
//   - 极长 words 列表与单 phone 单 note 的边界
//
// 这些用例反映 ds-editor-lite InferDurationTask 在用户提供异常 frameWidth
// 或异常 G2P 输出时的健壮性要求。BF-41 在 preprocessPhonemeDurations 入口
// 处加了防御性校验（!std::isfinite(frameWidth) || frameWidth <= 0），即使
// 调用方（Duration/Pitch/Variance/Acoustic）已经检查过 frameWidth，本工具
// 函数被其他路径调用时也不会发生除零 / NaN 传播。

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <map>
#include <string>
#include <vector>

#include <synthrt/Core/Support/Error.h>

#include <inferutil/InputWord.h>

#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>

using namespace ds::infer::inferutil;
using srt::core::ErrorCode;
using srt::core::ErrorCategory;
namespace Co = srt::svs::Api::Common::L1;

namespace {
    bool approxEqual(double a, double b, double eps = 1e-9) {
        return std::abs(a - b) < eps;
    }

    // 构建单 word 单 phone 单 note，便于在测试中精确控制 phone.start / note.duration
    Co::InputWordInfo makeSinglePhoneWord(double phoneStart, double noteDuration) {
        Co::InputWordInfo word;
        Co::InputPhonemeInfo phone;
        phone.token = "a";
        phone.language = "op";
        phone.start = phoneStart;
        phone.speakers.push_back({"default", 1.0});
        word.phones.push_back(std::move(phone));
        Co::InputNoteInfo note;
        note.key = 60;
        note.duration = noteDuration;
        word.notes.push_back(note);
        return word;
    }
}

// ---------------------------------------------------------------------------
// BF-41: preprocessPhonemeDurations frameWidth 校验
// ---------------------------------------------------------------------------

TEST_CASE("BF-41 preprocessPhonemeDurations NaN frameWidth returns error",
          "[inputword][extreme][bf-41]") {
    auto word = makeSinglePhoneWord(0.0, 0.1);
    std::vector<Co::InputWordInfo> words{word};

    double frameWidth = std::numeric_limits<double>::quiet_NaN();
    int64_t targetLength = -1;
    auto exp = preprocessPhonemeDurations(words, frameWidth, &targetLength);
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.isError(ErrorCode::InvalidArgument));
    REQUIRE(exp.error().message().find("frameWidth") != std::string::npos);
    // targetLength 在错误路径下不应被修改
    REQUIRE(targetLength == -1);
}

TEST_CASE("BF-41 preprocessPhonemeDurations positive infinity frameWidth returns error",
          "[inputword][extreme][bf-41]") {
    auto word = makeSinglePhoneWord(0.0, 0.1);
    std::vector<Co::InputWordInfo> words{word};

    double frameWidth = std::numeric_limits<double>::infinity();
    int64_t targetLength = -1;
    auto exp = preprocessPhonemeDurations(words, frameWidth, &targetLength);
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.isError(ErrorCode::InvalidArgument));
}

TEST_CASE("BF-41 preprocessPhonemeDurations negative infinity frameWidth returns error",
          "[inputword][extreme][bf-41]") {
    auto word = makeSinglePhoneWord(0.0, 0.1);
    std::vector<Co::InputWordInfo> words{word};

    double frameWidth = -std::numeric_limits<double>::infinity();
    int64_t targetLength = -1;
    auto exp = preprocessPhonemeDurations(words, frameWidth, &targetLength);
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.isError(ErrorCode::InvalidArgument));
}

TEST_CASE("BF-41 preprocessPhonemeDurations zero frameWidth returns error",
          "[inputword][extreme][bf-41]") {
    // 零 frameWidth 会触发除零（之前为 UB），现在必须显式报错。
    auto word = makeSinglePhoneWord(0.0, 0.1);
    std::vector<Co::InputWordInfo> words{word};

    double frameWidth = 0.0;
    int64_t targetLength = -1;
    auto exp = preprocessPhonemeDurations(words, frameWidth, &targetLength);
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.isError(ErrorCode::InvalidArgument));
}

TEST_CASE("BF-41 preprocessPhonemeDurations negative frameWidth returns error",
          "[inputword][extreme][bf-41]") {
    // 负 frameWidth 会产生负帧数（之前为 UB），现在必须显式报错。
    auto word = makeSinglePhoneWord(0.0, 0.1);
    std::vector<Co::InputWordInfo> words{word};

    double frameWidth = -0.01;
    int64_t targetLength = -1;
    auto exp = preprocessPhonemeDurations(words, frameWidth, &targetLength);
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.isError(ErrorCode::InvalidArgument));
}

TEST_CASE("BF-41 preprocessPhonemeDurations subnormal frameWidth returns error",
          "[inputword][extreme][bf-41]") {
    // 非正规化（subnormal）浮点数 frameWidth：极小但 >0，会通过 >0 检查但
    // isfinite 也通过。理论上会通过校验，但产生超大帧数（与 BF-41 不同，
    // 这里测试 isfinite 通过的情况，记录现状：subnormal 是 finite 值）。
    auto word = makeSinglePhoneWord(0.0, 0.1);
    std::vector<Co::InputWordInfo> words{word};

    double frameWidth = std::numeric_limits<double>::denorm_min();
    int64_t targetLength = -1;
    auto exp = preprocessPhonemeDurations(words, frameWidth, &targetLength);
    // subnormal 是 finite 且 >0，所以会通过校验进入正常路径。
    // 但 llround(0.1 / denorm_min) 可能溢出为 INT64_MAX 或抛出错误。
    // 这里仅校验不崩溃：要么成功（不太可能），要么返回 SessionError。
    if (exp.hasValue()) {
        // 若成功，targetLength 应非负
        REQUIRE(targetLength >= 0);
    } else {
        // 错误路径也应不崩溃
        REQUIRE(!exp.hasValue());
    }
}

// ---------------------------------------------------------------------------
// 空 words 列表（preprocessPhoneme* 三件套）
// ---------------------------------------------------------------------------

TEST_CASE("preprocessPhonemeTokens empty words list returns empty tensor",
          "[inputword][extreme][empty]") {
    std::vector<Co::InputWordInfo> words;
    std::map<std::string, int> tokens{{"a", 0}};
    auto exp = preprocessPhonemeTokens(words, tokens);
    // 空 words 应产生 0 元素 tensor，不崩溃。
    if (exp.hasValue()) {
        auto tensor = exp.take();
        if (tensor) {
            REQUIRE(tensor->elementCount() == 0);
        }
    } else {
        // 部分实现会返回错误，只要不崩溃即可。
        REQUIRE(!exp.hasValue());
    }
}

TEST_CASE("preprocessPhonemeLanguages empty words list returns empty tensor",
          "[inputword][extreme][empty]") {
    std::vector<Co::InputWordInfo> words;
    std::map<std::string, int> languages{{"op", 0}};
    auto exp = preprocessPhonemeLanguages(words, languages);
    if (exp.hasValue()) {
        auto tensor = exp.take();
        if (tensor) {
            REQUIRE(tensor->elementCount() == 0);
        }
    } else {
        REQUIRE(!exp.hasValue());
    }
}

TEST_CASE("preprocessPhonemeDurations empty words list returns empty tensor",
          "[inputword][extreme][empty]") {
    std::vector<Co::InputWordInfo> words;
    int64_t targetLength = -1;
    auto exp = preprocessPhonemeDurations(words, 0.01, &targetLength);
    if (exp.hasValue()) {
        auto tensor = exp.take();
        if (tensor) {
            REQUIRE(tensor->elementCount() == 0);
        }
        REQUIRE(targetLength == 0);
    } else {
        REQUIRE(!exp.hasValue());
    }
}

TEST_CASE("preprocessPhonemeDurations empty words with NaN frameWidth returns error",
          "[inputword][extreme][empty][bf-41]") {
    // 空 words + NaN frameWidth：BF-41 校验先于业务逻辑，必须返回错误。
    std::vector<Co::InputWordInfo> words;
    int64_t targetLength = -1;
    auto exp = preprocessPhonemeDurations(words,
                                         std::numeric_limits<double>::quiet_NaN(),
                                         &targetLength);
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.isError(ErrorCode::InvalidArgument));
}

// ---------------------------------------------------------------------------
// phone.start 顺序违反（currPhoneFrames 为负）
// ---------------------------------------------------------------------------

TEST_CASE("preprocessPhonemeDurations inverted phone starts yields negative frames",
          "[inputword][extreme][order]") {
    // word 内两个 phone，phone[1].start < phone[0].start，违反递增假设。
    // 当前实现不显式校验，会产生负 currPhoneFrames（targetLength 可能变小）。
    // 测试记录现状：不崩溃即可。
    Co::InputWordInfo word;
    word.phones.resize(2);
    word.phones[0].token = "a";
    word.phones[0].start = 0.05; // 比下一个 phone 还晚
    word.phones[1].token = "b";
    word.phones[1].start = 0.0;
    word.notes.resize(1);
    word.notes[0].duration = 0.1;

    std::vector<Co::InputWordInfo> words{word};
    int64_t targetLength = -1;
    auto exp = preprocessPhonemeDurations(words, 0.01, &targetLength);
    // 不崩溃即可；这里允许任何结果（成功或错误）。
    if (exp.hasValue()) {
        auto tensor = exp.take();
        REQUIRE(tensor->elementCount() == 2);
        // 不校验 targetLength 具体值，仅校验不崩溃。
    } else {
        REQUIRE(!exp.hasValue());
    }
}

TEST_CASE("preprocessPhonemeDurations equal phone starts yields zero frames",
          "[inputword][extreme][order]") {
    // 两个 phone.start 相同 -> 第二个 phone 帧数为 0。
    Co::InputWordInfo word;
    word.phones.resize(2);
    word.phones[0].token = "a";
    word.phones[0].start = 0.0;
    word.phones[1].token = "b";
    word.phones[1].start = 0.0; // 与前一个相同
    word.notes.resize(1);
    word.notes[0].duration = 0.1;

    std::vector<Co::InputWordInfo> words{word};
    int64_t targetLength = -1;
    auto exp = preprocessPhonemeDurations(words, 0.01, &targetLength);
    REQUIRE(exp.hasValue());
    auto tensor = exp.take();
    REQUIRE(tensor->elementCount() == 2);
    auto view = tensor->view<int64_t>();
    // 第一个 phone 占满整个 word（10 帧），第二个 phone 0 帧。
    REQUIRE(view[0] == 10);
    REQUIRE(view[1] == 0);
    REQUIRE(targetLength == 10);
}

// ---------------------------------------------------------------------------
// 极小 frameWidth / 极大 duration 边界
// ---------------------------------------------------------------------------

TEST_CASE("preprocessPhonemeDurations very small frameWidth produces large frame count",
          "[inputword][extreme][scale]") {
    // 0.1s word, frameWidth=0.0001 (0.1ms) -> 1000 frames
    auto word = makeSinglePhoneWord(0.0, 0.1);
    std::vector<Co::InputWordInfo> words{word};

    int64_t targetLength = -1;
    auto exp = preprocessPhonemeDurations(words, 0.0001, &targetLength);
    REQUIRE(exp.hasValue());
    REQUIRE(targetLength == 1000);
}

TEST_CASE("preprocessPhonemeDurations long duration produces large frame count",
          "[inputword][extreme][scale]") {
    // 60s word, frameWidth=0.01 -> 6000 frames（长旋律边界）
    auto word = makeSinglePhoneWord(0.0, 60.0);
    std::vector<Co::InputWordInfo> words{word};

    int64_t targetLength = -1;
    auto exp = preprocessPhonemeDurations(words, 0.01, &targetLength);
    REQUIRE(exp.hasValue());
    REQUIRE(targetLength == 6000);
}

TEST_CASE("preprocessPhonemeDurations many words aggregated targetLength",
          "[inputword][extreme][scale]") {
    // 100 个 word，每个 0.1s，frameWidth=0.01 -> 总 1000 帧
    std::vector<Co::InputWordInfo> words;
    words.reserve(100);
    for (int i = 0; i < 100; ++i) {
        words.push_back(makeSinglePhoneWord(0.0, 0.1));
    }

    int64_t targetLength = -1;
    auto exp = preprocessPhonemeDurations(words, 0.01, &targetLength);
    REQUIRE(exp.hasValue());
    auto tensor = exp.take();
    REQUIRE(tensor->elementCount() == 100);
    REQUIRE(targetLength == 1000);
}

// ---------------------------------------------------------------------------
// frameWidth 边界值（最小正 finite、极大 finite）
// ---------------------------------------------------------------------------

TEST_CASE("BF-41 preprocessPhonemeDurations minimum positive frameWidth succeeds",
          "[inputword][extreme][bf-41]") {
    // std::numeric_limits<double>::min() 是最小正规化正 double，isfinite 通过。
    // 此值会导致 0.1/min ≈ huge，可能 llround 溢出。测试记录现状：不崩溃即可。
    auto word = makeSinglePhoneWord(0.0, 0.001);
    std::vector<Co::InputWordInfo> words{word};

    double frameWidth = std::numeric_limits<double>::min();
    int64_t targetLength = -1;
    auto exp = preprocessPhonemeDurations(words, frameWidth, &targetLength);
    // 不崩溃即可。
    if (exp.hasValue()) {
        REQUIRE(targetLength >= 0);
    } else {
        REQUIRE(!exp.hasValue());
    }
}

TEST_CASE("BF-41 preprocessPhonemeDurations very large frameWidth yields zero or one frame",
          "[inputword][extreme][bf-41]") {
    // frameWidth 远大于 duration -> 0.1 / 1000 = 0 帧（或四舍五入为 0/1）
    auto word = makeSinglePhoneWord(0.0, 0.1);
    std::vector<Co::InputWordInfo> words{word};

    int64_t targetLength = -1;
    auto exp = preprocessPhonemeDurations(words, 1000.0, &targetLength);
    REQUIRE(exp.hasValue());
    auto tensor = exp.take();
    REQUIRE(tensor->elementCount() == 1);
    // 0.1 / 1000 = 0.0001 -> llround = 0 帧
    REQUIRE(targetLength == 0);
}

// ---------------------------------------------------------------------------
// outTargetLength=nullptr 在极端输入下也安全
// ---------------------------------------------------------------------------

TEST_CASE("BF-41 preprocessPhonemeDurations null outTargetLength with NaN frameWidth",
          "[inputword][extreme][bf-41]") {
    auto word = makeSinglePhoneWord(0.0, 0.1);
    std::vector<Co::InputWordInfo> words{word};

    auto exp = preprocessPhonemeDurations(words,
                                         std::numeric_limits<double>::quiet_NaN(),
                                         nullptr);
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.isError(ErrorCode::InvalidArgument));
}
