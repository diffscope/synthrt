// S2P 策略单元测试：DirectS2P / DictionaryS2P / MappingS2P
//
// 覆盖 ROBUST-01 (Expected 传播错误)、ROBUST-05 (出错显式报错) 与
// docs/modules/s2p.md 中描述的 S2P 错误码契约 (S2pDictionaryError /
// S2pConversionFailed，500-599 段)。
//
// 测试目标：
//   - DirectS2P::convert 空格切分的边界场景 (空串/前导/尾随/连续空格)
//   - DictionaryS2P::create TSV 解析的全部错误路径
//     (missing tab / multiple tabs / empty pronunciation / empty phoneme
//     sequence / empty phoneme / duplicate pronunciation / CRLF / 坏流)
//   - DictionaryS2P::convert 命中/未命中行为
//   - MappingS2P::create TSV 解析的全部错误路径 (与 DictionaryS2P 镜像)
//   - MappingS2P::convert 已映射/未映射/混合 phoneme 行为
//   - LanguageResource::direct / dictionary 工厂基本可构造性
//
// 这些测试不依赖 LuaJIT，因此始终参与编译 (与 test_lua_disabled.cpp 的
// if(NOT SYNTHRT_ENABLE_LUAJIT) 守卫解耦)。
//
// 真实使用场景参考 (ds-editor-lite)：
//   - SynthrtEngine 通过 LanguageService.resolveLanguageRoute 获取 s2pMode，
//     s2pMode == "dict" 时使用字典文件路径构造 DictionaryS2P；
//     s2pMode == "direct" 时使用 DirectS2P 直接切分。
//   - 字典文件由声库包提供 (assets/cmn.txt 等)，格式为 TSV。
//   - 字典解析失败必须显式报错 (ROBUST-05)，不能静默吞掉行。

#include <memory>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <synthrt/Core/Support/Expected.h>
#include <synthrt/S2P/DictionaryS2P.h>
#include <synthrt/S2P/DirectS2P.h>
#include <synthrt/S2P/LanguageResource.h>
#include <synthrt/S2P/MappingS2P.h>

using srt::core::ErrorCode;
using srt::core::Expected;
using srt::s2p::DictionaryS2P;
using srt::s2p::DirectS2P;
using srt::s2p::LanguageResource;
using srt::s2p::MappingS2P;

// ===========================================================================
// DirectS2P::convert — 按空格切分的边界场景
//
// 实现见 lib/S2P/DirectS2P.cpp：循环 find(' ')，跳过空 token 以容忍
// 前导/连续/尾随空格 (DictStep 历史版本会附加尾随空格)。
// ===========================================================================

TEST_CASE("DirectS2P splits by ASCII space", "[s2p][direct]") {
    REQUIRE(DirectS2P::convert("a b c") ==
            std::vector<std::string>{"a", "b", "c"});
}

TEST_CASE("DirectS2P returns empty for empty input", "[s2p][direct][edge]") {
    REQUIRE(DirectS2P::convert("").empty());
}

TEST_CASE("DirectS2P returns empty for whitespace-only input", "[s2p][direct][edge]") {
    REQUIRE(DirectS2P::convert("   ").empty());
}

TEST_CASE("DirectS2P trims leading space", "[s2p][direct][edge]") {
    REQUIRE(DirectS2P::convert(" a") == std::vector<std::string>{"a"});
}

TEST_CASE("DirectS2P trims trailing space", "[s2p][direct][edge]") {
    // DictStep 历史版本会在 pronunciation 末尾附加空格，DirectS2P 必须容忍。
    REQUIRE(DirectS2P::convert("a ") == std::vector<std::string>{"a"});
}

TEST_CASE("DirectS2P collapses consecutive spaces", "[s2p][direct][edge]") {
    REQUIRE(DirectS2P::convert("a  b   c") ==
            std::vector<std::string>{"a", "b", "c"});
}

TEST_CASE("DirectS2P handles single token", "[s2p][direct]") {
    REQUIRE(DirectS2P::convert("hello") == std::vector<std::string>{"hello"});
}

TEST_CASE("DirectS2P preserves phoneme content with internal tabs", "[s2p][direct]") {
    // DirectS2P 只按空格切分，不处理 tab；tab 是 phoneme 内容的一部分。
    // 这与 DictionaryS2P/MappingS2P 的 TSV 解析行为不同 (那里 tab 是分隔符)。
    auto result = DirectS2P::convert("a\tb c");
    REQUIRE(result.size() == 2);
    REQUIRE(result[0] == "a\tb");
    REQUIRE(result[1] == "c");
}

TEST_CASE("DirectS2P can be constructed and moved", "[s2p][direct][lifecycle]") {
    DirectS2P a;
    DirectS2P b(std::move(a));
    DirectS2P c;
    c = std::move(b);
    // 仅验证 move 构造/赋值不崩溃；DirectS2P::convert 是 static，不依赖实例状态。
    REQUIRE(DirectS2P::convert("x y") == std::vector<std::string>{"x", "y"});
}

// ===========================================================================
// DictionaryS2P::create — TSV 解析全部错误路径
//
// 实现见 lib/S2P/DictionaryS2P.cpp：每行按 tab 切分为 pronunciation + phonemes。
// 错误返回 ErrorCode::S2pDictionaryError 并附带行号上下文 (ROBUST-05)。
// ===========================================================================

TEST_CASE("DictionaryS2P accepts empty stream", "[s2p][dict][create]") {
    std::istringstream empty;
    auto result = DictionaryS2P::create(empty);
    REQUIRE(result.hasValue());
    REQUIRE(result->get() != nullptr);
    // 空字典查询任意 pronunciation 都返回空 vector。
    REQUIRE((*result)->convert("anything").empty());
}

TEST_CASE("DictionaryS2P parses well-formed TSV", "[s2p][dict][create]") {
    std::istringstream input{"hello\th e l l o\nworld\tw o r l d\n"};
    auto result = DictionaryS2P::create(input);
    REQUIRE(result.hasValue());
    auto &dict = *result;
    REQUIRE(dict->convert("hello") == std::vector<std::string>{"h", "e", "l", "l", "o"});
    REQUIRE(dict->convert("world") == std::vector<std::string>{"w", "o", "r", "l", "d"});
}

TEST_CASE("DictionaryS2P strips CRLF line endings", "[s2p][dict][create][crlf]") {
    // 声库包字典文件可能在 Windows 上以 CRLF 行尾保存，必须容忍。
    std::istringstream input{"hello\th e l l o\r\nworld\tw o r l d\r\n"};
    auto result = DictionaryS2P::create(input);
    REQUIRE(result.hasValue());
    REQUIRE((*result)->convert("hello") ==
            std::vector<std::string>{"h", "e", "l", "l", "o"});
    REQUIRE((*result)->convert("world") ==
            std::vector<std::string>{"w", "o", "r", "l", "d"});
}

TEST_CASE("DictionaryS2P rejects line without tab", "[s2p][dict][create][error]") {
    std::istringstream input{"hello world\n"};
    auto result = DictionaryS2P::create(input);
    REQUIRE(!result.hasValue());
    auto err = result.takeError();
    REQUIRE(err.code() == ErrorCode::S2pDictionaryError);
    // 错误消息必须包含行号 (ROBUST-05 显式报错)。
    REQUIRE(err.message().find("line 1") != std::string::npos);
    REQUIRE(err.message().find("missing tab separator") != std::string::npos);
}

TEST_CASE("DictionaryS2P rejects line with multiple tabs", "[s2p][dict][create][error]") {
    std::istringstream input{"hello\ta\tb\n"};
    auto result = DictionaryS2P::create(input);
    REQUIRE(!result.hasValue());
    auto err = result.takeError();
    REQUIRE(err.code() == ErrorCode::S2pDictionaryError);
    REQUIRE(err.message().find("multiple tab separators") != std::string::npos);
}

TEST_CASE("DictionaryS2P rejects empty pronunciation", "[s2p][dict][create][error]") {
    std::istringstream input{"\ta b c\n"};
    auto result = DictionaryS2P::create(input);
    REQUIRE(!result.hasValue());
    auto err = result.takeError();
    REQUIRE(err.code() == ErrorCode::S2pDictionaryError);
    REQUIRE(err.message().find("empty pronunciation") != std::string::npos);
}

TEST_CASE("DictionaryS2P rejects empty phoneme sequence", "[s2p][dict][create][error]") {
    std::istringstream input{"hello\t\n"};
    auto result = DictionaryS2P::create(input);
    REQUIRE(!result.hasValue());
    auto err = result.takeError();
    REQUIRE(err.code() == ErrorCode::S2pDictionaryError);
    REQUIRE(err.message().find("empty phoneme sequence") != std::string::npos);
}

TEST_CASE("DictionaryS2P rejects empty phoneme in sequence", "[s2p][dict][create][error]") {
    // 音素序列包含连续空格会切分出空 phoneme，必须报错。
    std::istringstream input{"hello\ta  b\n"};
    auto result = DictionaryS2P::create(input);
    REQUIRE(!result.hasValue());
    auto err = result.takeError();
    REQUIRE(err.code() == ErrorCode::S2pDictionaryError);
    REQUIRE(err.message().find("empty phoneme") != std::string::npos);
    // 不能误报为 "empty phoneme sequence" (那是行尾没内容的情况)。
    REQUIRE(err.message().find("empty phoneme sequence") == std::string::npos);
}

TEST_CASE("DictionaryS2P rejects duplicate pronunciation", "[s2p][dict][create][error]") {
    std::istringstream input{"hello\ta\nhello\tb\n"};
    auto result = DictionaryS2P::create(input);
    REQUIRE(!result.hasValue());
    auto err = result.takeError();
    REQUIRE(err.code() == ErrorCode::S2pDictionaryError);
    // 行号必须是第二行 (出现重复的那一行)，不是第一行。
    REQUIRE(err.message().find("line 2") != std::string::npos);
    REQUIRE(err.message().find("duplicate pronunciation") != std::string::npos);
}

TEST_CASE("DictionaryS2P error line number is accurate across multiple lines",
          "[s2p][dict][create][error]") {
    // 前两行有效，第三行缺少 tab — 错误消息应报告 line 3。
    std::istringstream input{"a\tx\nb\ty\nbroken line\n"};
    auto result = DictionaryS2P::create(input);
    REQUIRE(!result.hasValue());
    auto err = result.takeError();
    REQUIRE(err.message().find("line 3") != std::string::npos);
}

TEST_CASE("DictionaryS2P convert returns empty when not found",
          "[s2p][dict][convert]") {
    std::istringstream input{"hello\th e l l o\n"};
    auto result = DictionaryS2P::create(input);
    REQUIRE(result.hasValue());
    // 未命中时返回空 vector (头文件契约)。
    REQUIRE((*result)->convert("unknown").empty());
    // 空 pronunciation 也未命中。
    REQUIRE((*result)->convert("").empty());
}

TEST_CASE("DictionaryS2P can be moved", "[s2p][dict][lifecycle]") {
    std::istringstream input{"a\tx\n"};
    auto result = DictionaryS2P::create(input);
    REQUIRE(result.hasValue());
    auto &owned = *result;
    DictionaryS2P moved(std::move(*owned));
    REQUIRE(moved.convert("a") == std::vector<std::string>{"x"});
}

// ===========================================================================
// MappingS2P::create — TSV 解析全部错误路径
//
// 实现见 lib/S2P/MappingS2P.cpp：与 DictionaryS2P 镜像，但错误码是
// ErrorCode::S2pConversionFailed (而非 S2pDictionaryError)，因为映射表
// 解析失败属于转换阶段错误，不是字典查询错误。
// ===========================================================================

TEST_CASE("MappingS2P accepts empty stream", "[s2p][mapping][create]") {
    std::istringstream empty;
    auto result = MappingS2P::create(empty);
    REQUIRE(result.hasValue());
    REQUIRE(result->get() != nullptr);
    // 空映射表 = DirectS2P 透传。
    REQUIRE((*result)->convert("a b c") ==
            std::vector<std::string>{"a", "b", "c"});
}

TEST_CASE("MappingS2P parses well-formed TSV", "[s2p][mapping][create]") {
    std::istringstream input{"a\tA\nb\tB\n"};
    auto result = MappingS2P::create(input);
    REQUIRE(result.hasValue());
    // 已映射的 phoneme 被替换。
    REQUIRE((*result)->convert("a b") ==
            std::vector<std::string>{"A", "B"});
}

TEST_CASE("MappingS2P strips CRLF line endings", "[s2p][mapping][create][crlf]") {
    std::istringstream input{"a\tA\r\nb\tB\r\n"};
    auto result = MappingS2P::create(input);
    REQUIRE(result.hasValue());
    REQUIRE((*result)->convert("a b") == std::vector<std::string>{"A", "B"});
}

TEST_CASE("MappingS2P rejects line without tab", "[s2p][mapping][create][error]") {
    std::istringstream input{"hello\n"};
    auto result = MappingS2P::create(input);
    REQUIRE(!result.hasValue());
    auto err = result.takeError();
    REQUIRE(err.code() == ErrorCode::S2pConversionFailed);
    REQUIRE(err.message().find("missing tab separator") != std::string::npos);
    REQUIRE(err.message().find("line 1") != std::string::npos);
}

TEST_CASE("MappingS2P rejects line with multiple tabs", "[s2p][mapping][create][error]") {
    std::istringstream input{"a\tA\tX\n"};
    auto result = MappingS2P::create(input);
    REQUIRE(!result.hasValue());
    auto err = result.takeError();
    REQUIRE(err.code() == ErrorCode::S2pConversionFailed);
    REQUIRE(err.message().find("multiple tab separators") != std::string::npos);
}

TEST_CASE("MappingS2P rejects empty original phoneme", "[s2p][mapping][create][error]") {
    std::istringstream input{"\tA\n"};
    auto result = MappingS2P::create(input);
    REQUIRE(!result.hasValue());
    auto err = result.takeError();
    REQUIRE(err.code() == ErrorCode::S2pConversionFailed);
    REQUIRE(err.message().find("empty original phoneme") != std::string::npos);
}

TEST_CASE("MappingS2P rejects empty target phoneme", "[s2p][mapping][create][error]") {
    std::istringstream input{"a\t\n"};
    auto result = MappingS2P::create(input);
    REQUIRE(!result.hasValue());
    auto err = result.takeError();
    REQUIRE(err.code() == ErrorCode::S2pConversionFailed);
    REQUIRE(err.message().find("empty target phoneme") != std::string::npos);
}

TEST_CASE("MappingS2P rejects duplicate original phoneme",
          "[s2p][mapping][create][error]") {
    std::istringstream input{"a\tA\na\tB\n"};
    auto result = MappingS2P::create(input);
    REQUIRE(!result.hasValue());
    auto err = result.takeError();
    REQUIRE(err.code() == ErrorCode::S2pConversionFailed);
    REQUIRE(err.message().find("line 2") != std::string::npos);
    REQUIRE(err.message().find("duplicate original phoneme") != std::string::npos);
}

// ===========================================================================
// MappingS2P::convert — 已映射/未映射/混合 phoneme 行为
// ===========================================================================

TEST_CASE("MappingS2P passes through unmapped phonemes", "[s2p][mapping][convert]") {
    std::istringstream input{"a\tA\n"};
    auto result = MappingS2P::create(input);
    REQUIRE(result.hasValue());
    // 'a' 已映射为 'A'；'b'/'c' 未映射，透传原值。
    REQUIRE((*result)->convert("a b c") ==
            std::vector<std::string>{"A", "b", "c"});
}

TEST_CASE("MappingS2P convert on empty input returns empty",
          "[s2p][mapping][convert][edge]") {
    std::istringstream input{"a\tA\n"};
    auto result = MappingS2P::create(input);
    REQUIRE(result.hasValue());
    REQUIRE((*result)->convert("").empty());
}

TEST_CASE("MappingS2P convert with empty mapping is pure DirectS2P",
          "[s2p][mapping][convert]") {
    std::istringstream empty;
    auto result = MappingS2P::create(empty);
    REQUIRE(result.hasValue());
    // 空映射 = DirectS2P 透传，所有 phoneme 不变。
    REQUIRE((*result)->convert("x y z") ==
            std::vector<std::string>{"x", "y", "z"});
}

TEST_CASE("MappingS2P can be moved", "[s2p][mapping][lifecycle]") {
    std::istringstream input{"a\tA\n"};
    auto result = MappingS2P::create(input);
    REQUIRE(result.hasValue());
    auto &owned = *result;
    MappingS2P moved(std::move(*owned));
    REQUIRE(moved.convert("a") == std::vector<std::string>{"A"});
}

// ===========================================================================
// LanguageResource — direct / dictionary 工厂基本可构造性
//
// LanguageResource 是 move-only RAII 类型 (delete copy, default move)。
// 这里只验证工厂函数返回可用的对象，不深入 onset 规则 (需要 RuleOnsetMarker
// 实现，超出本测试范围)。
//
// 注意 (源码观察，非 bug)：LanguageResource::direct/dictionary 工厂函数
// 返回 LanguageResource (非 Expected<LanguageResource>)，文件不存在或解析
// 失败时抛 std::runtime_error。这与 ROBUST-01 (Expected 传播错误) 不一致，
// 但属于既有 API 设计，本测试不修改源码，只验证实际行为。
// ===========================================================================

TEST_CASE("LanguageResource::direct with no onset rule constructs and converts",
          "[s2p][resource]") {
    // 不传 onsetRulePath，直接构造 direct 模式资源 (不读文件，不抛异常)。
    auto res = LanguageResource::direct();
    // convert 行为由 DirectS2P 实现：按空格切分。
    auto pron = res.convert("a b c");
    REQUIRE(pron.phonemes == std::vector<std::string>{"a", "b", "c"});
    // 没有 onsetMarker 时 onsets 为空。
    REQUIRE(pron.onsets.empty());
}

TEST_CASE("LanguageResource::direct throws on missing onset rule file",
          "[s2p][resource][error]") {
    // 源码行为：onsetRulePath 非空时调用 readAll，文件不存在抛
    // std::runtime_error。这是 ROBUST-01 的偏离点，但既有 API 契约如此。
    REQUIRE_THROWS_AS(LanguageResource::direct("nonexistent/rule.txt"),
                      std::runtime_error);
}

TEST_CASE("LanguageResource::dictionary throws on missing dictionary file",
          "[s2p][resource][error]") {
    // 源码行为：dictionaryPath 非空时调用 readAll，文件不存在抛
    // std::runtime_error。Lite 调用方需 try-catch 包装。
    REQUIRE_THROWS_AS(LanguageResource::dictionary("nonexistent/dict.txt"),
                      std::runtime_error);
}

TEST_CASE("LanguageResource is move-only", "[s2p][resource][lifecycle]") {
    // 静态断言：copy 构造/赋值被 delete。
    static_assert(!std::is_copy_constructible<LanguageResource>::value,
                  "LanguageResource must be move-only");
    static_assert(!std::is_copy_assignable<LanguageResource>::value,
                  "LanguageResource must be move-only");
    static_assert(std::is_move_constructible<LanguageResource>::value,
                  "LanguageResource must be movable");
    static_assert(std::is_move_assignable<LanguageResource>::value,
                  "LanguageResource must be movable");

    auto res1 = LanguageResource::direct();
    LanguageResource res2(std::move(res1));
    auto pron = res2.convert("a");
    REQUIRE(pron.phonemes == std::vector<std::string>{"a"});
}

// ===========================================================================
// ds-editor-lite 真实使用场景 (文档化测试)
//
// Lite SynthrtEngine 根据 LanguageRoute.s2pMode 选择 S2P 策略：
//   - s2pMode == "dict"   → 加载声库包字典文件 (assets/cmn.txt 等)，
//                           构造 DictionaryS2P 进行查询。
//   - s2pMode == "direct" → 直接使用 DirectS2P 按空格切分 pronunciation。
//
// 字典文件由声库包提供，格式为 TSV：pronunciation\tphoneme1 phoneme2 ...
// 字典解析失败必须显式报错 (ROBUST-05)，Lite 据此向用户显示错误对话框，
// 而非静默吞掉行导致 G2P 输出错误音素。
// ===========================================================================

TEST_CASE("lite dict-mode TSV parsing matches DictionaryS2P contract",
          "[s2p][realworld][dict]") {
    // 模拟声库包字典文件内容：cmn 语言的几个常用 pronunciation。
    std::istringstream dictStream{
        "你好\tn i h a o\n"
        "世界\tsh i j ie\n"
        "你好吗\tn i h a o m a\n"};
    auto result = DictionaryS2P::create(dictStream);
    REQUIRE(result.hasValue());
    auto &dict = *result;
    REQUIRE(dict->convert("你好") ==
            std::vector<std::string>{"n", "i", "h", "a", "o"});
    REQUIRE(dict->convert("你好吗") ==
            std::vector<std::string>{"n", "i", "h", "a", "o", "m", "a"});
}

TEST_CASE("lite direct-mode splits pronunciation by space",
          "[s2p][realworld][direct]") {
    // direct 模式下，Lite 直接传入 G2P 输出的 phoneme 序列 (空格分隔)，
    // DirectS2P 切分后作为 S2P 结果。多音素按空格切分。
    auto phonemes = DirectS2P::convert("a b c d");
    REQUIRE(phonemes.size() == 4);
    REQUIRE(phonemes[0] == "a");
    REQUIRE(phonemes[3] == "d");
}

TEST_CASE("lite dict-mode with malformed dictionary reports explicit error",
          "[s2p][realworld][dict][error]") {
    // 模拟声库包字典文件损坏：缺少 tab 分隔符。
    // Lite 必须捕获 S2pDictionaryError 并向用户显示错误，而非静默跳过。
    std::istringstream badDict{"你好 ni hao\n"};
    auto result = DictionaryS2P::create(badDict);
    REQUIRE(!result.hasValue());
    auto err = result.takeError();
    REQUIRE(err.code() == ErrorCode::S2pDictionaryError);
    REQUIRE(err.message().find("missing tab separator") != std::string::npos);
}
