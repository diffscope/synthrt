// G2P 跨项目契约常量测试
//
// 覆盖 D-17 人工决策约束：跨项目常量必须使用而非字符串字面量。
// 这些常量是 synthrt 与 ds-editor-lite 之间的契约，任何一方修改字符串
// 值都会破坏对方调用，因此必须有单元测试锁定具体值。
//
// 测试目标：
//   - kG2pOnnxDriverName = "g2pOnnxDriver" (D-20 全局基础设施)
//   - kOfficialContext = "" (官方默认 context)
//   - kG2pSourceOfficial = "official"
//   - kG2pSourceVoicebank = "voicebank"
//   - 框架 category 名常量
//   - G2pRes mode 字符串常量 (convert/copy/skip)
//   - Plugin IID 常量 (srt.g2p.task / srt.g2p.driver)
//
// 源码定义见 include/synthrt/G2P/Base/LangCommon.h。
// Lite 使用方：SynthrtEngine::initializeG2pOnnxDriver / G2pInputAdapter::fromRoute。

#include <string>

#include <catch2/catch_test_macros.hpp>

#include <synthrt/G2P/Base/LangCommon.h>

using namespace srt::g2p;

// ---------------------------------------------------------------------------
// D-20: ONNX 驱动全局基础设施名
//
// Lite SynthrtEngine::initializeG2pOnnxDriver 在 Manager::initialize 之前注册
// g2pOnnxDriver。字符串值必须与 Lite 调用方完全一致。
// ---------------------------------------------------------------------------
TEST_CASE("kG2pOnnxDriverName is g2pOnnxDriver", "[g2p][constant][d-20]") {
    REQUIRE(std::string(kG2pOnnxDriverName) == "g2pOnnxDriver");
}

// ---------------------------------------------------------------------------
// 官方默认 context 标识（空字符串）
//
// voicebankContextName = packageId + "__" + singerId 不使用 ":" 因为
// ContextUtils::validateContextName 禁用 ":"（保留给 FQID 分隔）。
// kOfficialContext = "" 表示官方默认上下文。
// ---------------------------------------------------------------------------
TEST_CASE("kOfficialContext is empty string", "[g2p][constant]") {
    REQUIRE(std::string(kOfficialContext).empty());
}

// ---------------------------------------------------------------------------
// g2pSource 常量
// ---------------------------------------------------------------------------
TEST_CASE("kG2pSourceOfficial is official", "[g2p][constant]") {
    REQUIRE(std::string(kG2pSourceOfficial) == "official");
}

TEST_CASE("kG2pSourceVoicebank is voicebank", "[g2p][constant][d-17]") {
    // D-17: 跨项目常量必须使用 kG2pSourceVoicebank 而非字符串字面量
    REQUIRE(std::string(kG2pSourceVoicebank) == "voicebank");
}

// ---------------------------------------------------------------------------
// 框架 category 名常量
// ---------------------------------------------------------------------------
TEST_CASE("Framework category name constants", "[g2p][constant][category]") {
    REQUIRE(std::string(kG2pCategory) == "g2p");
    REQUIRE(std::string(kDictCategory) == "dict");
    REQUIRE(std::string(kDriverCategory) == "driver");
}

// ---------------------------------------------------------------------------
// D-17: G2pRes mode 字符串常量
//
// mode 字段用于区分：
//   - "convert": 经模型/字典真正转换得到发音
//   - "copy": 原词保留（回退、降级、标点/数字等未真正转换）
//   - "skip": 空 lyric 跳过（pronunciation 为空）
// Lite 根据 mode 字段决定 UI 显示和后续处理。
// ---------------------------------------------------------------------------
TEST_CASE("G2pRes mode constants", "[g2p][constant][mode][d-17]") {
    REQUIRE(std::string(kG2pModeConvert) == "convert");
    REQUIRE(std::string(kG2pModeCopy) == "copy");
    REQUIRE(std::string(kG2pModeSkip) == "skip");
}

// ---------------------------------------------------------------------------
// Plugin IID 常量 (P3.2 迁移到 srt.g2p.* IIDs)
// ---------------------------------------------------------------------------
TEST_CASE("Plugin IID constants", "[g2p][constant][iid]") {
    REQUIRE(std::string(kTaskPluginIid) == "srt.g2p.task");
    REQUIRE(std::string(kDriverPluginIid) == "srt.g2p.driver");
}

// ---------------------------------------------------------------------------
// G2pInput / G2pRes 默认值与构造
//
// Lite G2pInputAdapter::fromRoute 构造 G2pInput，G2pConvertRunner 消费它。
// 验证默认值确保 Lite 不会因默认值变化而崩溃。
// ---------------------------------------------------------------------------
TEST_CASE("G2pInput default construction", "[g2p][input][default]") {
    G2pInput in;
    REQUIRE(in.lyric.empty());
    REQUIRE(in.g2pId.empty());
    REQUIRE(in.g2pContext.empty());
    REQUIRE(in.g2pContextVersion.isEmpty());
}

TEST_CASE("G2pInput constructor fills fields", "[g2p][input][ctor]") {
    G2pInput in("hello", "eng-g2p", "voicebank-A");
    REQUIRE(in.lyric == "hello");
    REQUIRE(in.g2pId == "eng-g2p");
    REQUIRE(in.g2pContext == "voicebank-A");
}

TEST_CASE("G2pRes default mode is copy", "[g2p][res][default]") {
    G2pRes r;
    REQUIRE(r.mode == kG2pModeCopy);
    REQUIRE(r.errorType == NoError);
    REQUIRE(r.isOk());
    REQUIRE_FALSE(r.isFailed());
}

TEST_CASE("G2pRes isOk/isFailed respect errorType", "[g2p][res][state]") {
    SECTION("NoError is ok") {
        G2pRes r;
        r.errorType = NoError;
        REQUIRE(r.isOk());
        REQUIRE_FALSE(r.isFailed());
    }
    SECTION("ModelInferenceFailed is failed") {
        G2pRes r;
        r.errorType = ModelInferenceFailed;
        REQUIRE_FALSE(r.isOk());
        REQUIRE(r.isFailed());
    }
}

// ---------------------------------------------------------------------------
// G2pRes 构造函数：空 pronunciation 回退到 lyric
// ---------------------------------------------------------------------------
TEST_CASE("G2pRes empty pronunciation falls back to lyric", "[g2p][res][fallback]") {
    G2pRes r("hello", "eng-g2p");
    REQUIRE(r.pronunciation == "hello");
    REQUIRE(r.candidates.size() == 1);
    REQUIRE(r.candidates[0] == "hello");
}

TEST_CASE("G2pRes with pronunciation and candidates", "[g2p][res][ctor]") {
    std::vector<std::string> cands = {"a", "b"};
    G2pRes r("hello", "eng-g2p", "voicebank-A", {}, "HH AH L OW", cands);
    REQUIRE(r.pronunciation == "HH AH L OW");
    REQUIRE(r.candidates.size() == 2);
}

// ---------------------------------------------------------------------------
// G2pErrorType 枚举值
// ---------------------------------------------------------------------------
TEST_CASE("G2pErrorType enum values are stable", "[g2p][error-type]") {
    // 这些值跨版本必须稳定，Lite 可能根据 errorType 做分支
    REQUIRE(NoError == 0);
    REQUIRE(InvalidLyric == 1);
    REQUIRE(ModelInferenceFailed == 2);
    REQUIRE(PhonemeGenerationFailed == 3);
    REQUIRE(DriverUnavailable == 4);
    REQUIRE(NotInitialized == 5);
    REQUIRE(UnknownError == 6);
}

// ---------------------------------------------------------------------------
// ds-editor-lite 真实使用场景：LanguageRoute 字段语义
//
// Lite G2pService::convert 使用 routeCache 缓存 LanguageRoute，并通过
// G2pInputAdapter::fromRoute 构造 G2pInput。route 的 g2pSource 字段决定
// Lite 是否走声库私有 G2P 路径。
// 这里验证 LanguageRoute 的默认值符合 Lite 假设。
// ---------------------------------------------------------------------------
TEST_CASE("LanguageRoute default construction matches lite assumptions",
          "[g2p][route][realworld]") {
    srt::g2p::LanguageRoute route;
    REQUIRE(route.g2pId.empty());
    REQUIRE(route.g2pContext.empty()); // 空表示官方默认上下文
    REQUIRE(route.g2pContextVersion.isEmpty());
    REQUIRE(route.g2pSource.empty());
    REQUIRE(route.s2pMode.empty());
    REQUIRE(route.s2pFile.empty());
    REQUIRE(route.onsetFile.empty());
}

TEST_CASE("LanguageRoute official source matches kG2pSourceOfficial",
          "[g2p][route][realworld]") {
    // Lite 检查 route.g2pSource == kG2pSourceOfficial 时跳过声库私有路径
    srt::g2p::LanguageRoute route;
    route.g2pSource = kG2pSourceOfficial;
    REQUIRE(route.g2pSource == kG2pSourceOfficial);
    REQUIRE(route.g2pSource != kG2pSourceVoicebank);
}
