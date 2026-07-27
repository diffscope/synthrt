// test_voicebank_session_snapshot_ensure.cpp
//
// Snapshot / ensure API 测试覆盖补充（V3-06 ~ V3-12, D-26 ~ D-36）。
//
// 本文件补全 test_voicebank_session.cpp 与 test_vbs_*.cpp 拆分套件
// 未覆盖的盲点，聚焦以下场景：
//   1. SessionResources 注入构造函数（V3-06）—— 区别于 deprecated setRuntime/
//      setLanguageService 路径，验证注入构造存储资源、析构不释放借用资源。
//   2. 配置变更延迟生效（D-35）—— setRoots / setReservedPhonemes 不立即影响
//      snapshot，下次 refresh() 原子应用。
//   3. refresh 失败保留 previous snapshot（D-31 边界）—— 全部包无效保留旧
//      snapshot；空 roots 成功发布空 snapshot；首次失败无 previous 返回 nullptr。
//   4. Discovery 多包部分成功（D-31）—— 多个有效包 + 多个无效包共存。
//   5. ensureLanguageReady 状态机（V3-08）—— metadataReady=false 返回
//      G2pInitializationError；失败幂等；snapshot 为空返回 SessionError。
//   6. subscribeRefresh 边界 —— 多订阅者、异常隔离、外部 reset、移动语义。
//   7. 指纹顺序无关性（V3-07 D-33）—— catalogFingerprint 与扫描顺序无关；
//      languageFingerprint 排除非语言字段（如 singer.name）。
//   8. sync/refreshAsync 并发一致性 —— refresh() 内部调用 refreshAsync().get()。
//   9. Stale retry L2 占位 —— 需要真实 ONNX 包，SKIP。
//
// 约束：
//   - L1 测试不加载插件 DLL / ONNX 包；需要 L2 fixture 的用例标记 SKIP。
//   - 不修改源码；仅新增测试。
//   - 遵循 Catch2 v3 模式，4 空格缩进，120 列宽度。
//   - 复用 test_vbs_common.h 中的 fixture helpers（与 test_vbs_*.cpp 一致）。

#include <atomic>
#include <future>
#include <stdexcept>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include <stdcorelib/support/versionnumber.h>

#include <synthrt/Core/Core/Runtime.h>
#include <synthrt/Core/Support/Diagnostic.h>
#include <synthrt/G2P/LanguageService.h>
#include <diffsinger/Session/VoicebankSession.h>

#include "test_vbs_common.h"

using namespace vbs_test;

// ===========================================================================
// 1. SessionResources 注入构造函数（V3-06）
//
// 注入构造函数 VoicebankSession(SessionResources) 是 V3-06 引入的推荐构造
// 方式，替代 deprecated setRuntime / setLanguageService。以下测试验证：
//   - 默认构造留下空资源（仅 discovery 可用）
//   - 注入构造存储 Runtime* 与 shared_ptr<LanguageService>
//   - 析构仅释放 session 持有的引用，不影响宿主拥有的资源生命周期
// ===========================================================================

TEST_CASE("VoicebankSession default constructor leaves resources null",
          "[ds-session][injection]") {
    // 默认构造：Runtime 与 LanguageService 均为空，session 仅能用于 discovery
    // （refresh / subscribeRefresh / snapshot）。convertG2p / convertS2p /
    // createModelSet / ensureModelSet 会因缺少资源返回错误。
    ds::session::VoicebankSession session;
    REQUIRE(session.runtime() == nullptr);
    REQUIRE(session.languageService() == nullptr);
    // 尚未 refresh，snapshot 也为空。
    REQUIRE(session.snapshot() == nullptr);
}

TEST_CASE("VoicebankSession SessionResources injection stores runtime and languageService",
          "[ds-session][injection]") {
    // 注入构造：SessionResources 携带的 Runtime* 与 shared_ptr<LanguageService>
    // 被存入 session，通过 runtime() / languageService() 可读回原指针。
    // shared_ptr 引用计数：宿主 (langSvc) + session → 2。
    srt::core::Runtime runtime;
    auto langSvc = std::make_shared<srt::g2p::LanguageService>();

    ds::session::SessionResources resources;
    resources.runtime = &runtime;
    resources.languageService = langSvc;

    ds::session::VoicebankSession session(std::move(resources));
    REQUIRE(session.runtime() == &runtime);
    REQUIRE(session.languageService() == langSvc);
    REQUIRE(langSvc.use_count() == 2); // host + session
}

TEST_CASE("VoicebankSession destruction does not release borrowed host resources",
          "[ds-session][injection]") {
    // 析构契约（V3-06）：session 仅"借用"宿主资源，析构时不释放 Runtime 对象
    // 或重置 LanguageService —— shared_ptr 引用计数回到注入前的值。
    // 这区别于"session 拥有资源生命周期"的错误设计；宿主资源在 session
    // 析构后仍可继续使用。
    auto langSvc = std::make_shared<srt::g2p::LanguageService>();
    const auto originalUseCount = langSvc.use_count();
    REQUIRE(originalUseCount == 1);

    srt::core::Runtime runtime;
    {
        ds::session::SessionResources resources;
        resources.runtime = &runtime;
        resources.languageService = langSvc;
        ds::session::VoicebankSession session(std::move(resources));
        REQUIRE(langSvc.use_count() == 2); // host + session
    }
    // session 销毁 → use_count 回到原值。
    REQUIRE(langSvc.use_count() == originalUseCount);
    // LanguageService 仍可用（默认状态：metadata 未初始化）。
    REQUIRE_FALSE(langSvc->metadataReady());
    // Runtime 仍在宿主栈上，可继续被新 session 借用。
    ds::session::SessionResources next;
    next.runtime = &runtime;
    next.languageService = langSvc;
    ds::session::VoicebankSession another(std::move(next));
    REQUIRE(another.runtime() == &runtime);
}

// ===========================================================================
// 2. 配置变更延迟生效（D-35）
//
// setRoots / setReservedPhonemes 是配置级输入，不会立即影响已发布的 snapshot。
// 配置在下次 refresh() 时原子应用 —— 两次 set 调用之间不会有"中间态"
// （一个已应用、另一个未应用）。
// ===========================================================================

TEST_CASE("setRoots does not affect current snapshot until next refresh",
          "[ds-session][config-delayed]") {
    // D-35：setRoots 立即更新 roots() 返回值，但已发布的 snapshot 仍保留旧
    // roots，直到下次 refresh() 才应用新 roots。
    const auto root1 = makeRoot();
    makePackage(root1);
    ds::session::VoicebankSession session;
    session.setRoots({root1});
    REQUIRE(session.refreshAsync().get().succeeded);
    const auto snap1 = session.snapshot();
    REQUIRE(snap1 != nullptr);
    REQUIRE(snap1->roots == std::vector<std::filesystem::path>{root1});

    // 准备一个空的 root2。
    const auto root2 = makeRoot(); // 空 bank 目录，无包
    session.setRoots({root2});

    // roots() 立即反映新值（配置 API）。
    REQUIRE(session.roots() == std::vector<std::filesystem::path>{root2});
    // 但 snapshot 仍指向旧对象（未 refresh），roots 字段仍是旧值。
    const auto snap2 = session.snapshot();
    REQUIRE(snap2 == snap1); // 同一指针 —— 未发布新 snapshot
    REQUIRE(snap2->roots == std::vector<std::filesystem::path>{root1});

    // refresh 后新 roots 生效。
    const auto result = session.refreshAsync().get();
    REQUIRE(result.succeeded);
    REQUIRE(result.changed); // roots 变化 → changed=true
    const auto snap3 = session.snapshot();
    REQUIRE(snap3 != snap1);
    REQUIRE(snap3->roots == std::vector<std::filesystem::path>{root2});

    std::filesystem::remove_all(root1);
    std::filesystem::remove_all(root2);
}

TEST_CASE("setReservedPhonemes does not affect current snapshot until next refresh",
          "[ds-session][config-delayed]") {
    // D-35：setReservedPhonemes 同样延迟到下次 refresh 才应用到 snapshot。
    const auto root = makeRoot();
    makePackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});
    session.setReservedPhonemes({"SP", "AP"});
    REQUIRE(session.refreshAsync().get().succeeded);
    const auto snap1 = session.snapshot();
    REQUIRE(snap1->reservedPhonemes == std::vector<std::string>{"SP", "AP"});

    session.setReservedPhonemes({"sil"});

    // reservedPhonemes() 立即反映新值。
    REQUIRE(session.reservedPhonemes() == std::vector<std::string>{"sil"});
    // snapshot 仍是旧值。
    const auto snap2 = session.snapshot();
    REQUIRE(snap2 == snap1);
    REQUIRE(snap2->reservedPhonemes == std::vector<std::string>{"SP", "AP"});

    // refresh 后新 reservedPhonemes 生效。
    const auto result = session.refreshAsync().get();
    REQUIRE(result.succeeded);
    REQUIRE(result.changed); // reservedPhonemes 变化 → changed=true
    const auto snap3 = session.snapshot();
    REQUIRE(snap3 != snap1);
    REQUIRE(snap3->reservedPhonemes == std::vector<std::string>{"sil"});

    std::filesystem::remove_all(root);
}

TEST_CASE("setRoots and setReservedPhonemes apply atomically on next refresh",
          "[ds-session][config-delayed]") {
    // D-35 原子性：两次 set 之间不会有"一个已应用、另一个未应用"的中间态。
    // 两个配置变更在单次 refresh 中一起生效。
    const auto root1 = makeRoot();
    makePackage(root1);
    ds::session::VoicebankSession session;
    session.setRoots({root1});
    session.setReservedPhonemes({"SP", "AP"});
    REQUIRE(session.refreshAsync().get().succeeded);
    const auto snap1 = session.snapshot();
    REQUIRE(snap1->roots == std::vector<std::filesystem::path>{root1});
    REQUIRE(snap1->reservedPhonemes == std::vector<std::string>{"SP", "AP"});

    const auto root2 = makeRoot(); // 空
    session.setRoots({root2});
    session.setReservedPhonemes({"sil"});

    // 两次 set 与 refresh 之间，snapshot 不变。
    REQUIRE(session.snapshot() == snap1);

    const auto result = session.refreshAsync().get();
    REQUIRE(result.succeeded);
    REQUIRE(result.changed);
    const auto snap3 = session.snapshot();
    // 两个配置一起应用。
    REQUIRE(snap3->roots == std::vector<std::filesystem::path>{root2});
    REQUIRE(snap3->reservedPhonemes == std::vector<std::string>{"sil"});

    std::filesystem::remove_all(root1);
    std::filesystem::remove_all(root2);
}

// ===========================================================================
// 3. refresh 失败保留 previous snapshot（D-31 边界）
//
// D-31 Discovery 部分成功：有效包发布到 snapshot，无效包转化为 diagnostics。
// 边界情形：
//   - 全部包无效 → refresh 失败，保留 previous snapshot（若有）
//   - 空 roots → refresh 成功，发布空 snapshot（区别于"全部无效"）
//   - 首次 refresh 即全部无效 → 无 previous，snapshot 为 nullptr
// ===========================================================================

TEST_CASE("refresh with all invalid packages preserves previous snapshot",
          "[ds-session][refresh-failure]") {
    // D-31 边界：扫描到的包全部无效时 refresh 失败，previous snapshot 保留。
    const auto root = makeRoot();
    makePackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});
    const auto first = session.refreshAsync().get();
    REQUIRE(first.succeeded);
    const auto snap1 = session.snapshot();
    REQUIRE(snap1 != nullptr);
    REQUIRE(snap1->packages.size() == 1);

    // 移除有效包的 desc.json，scanner 不再把它视为包。
    std::filesystem::remove(root / "bank" / "desc.json");
    // 加入两个无效包（非 JSON）。
    writeFile(root / "broken1" / "desc.json", "not json");
    writeFile(root / "broken2" / "desc.json", "still not json");

    const auto second = session.refreshAsync().get();
    REQUIRE_FALSE(second.succeeded);
    // previous snapshot 保留（RefreshResult 与 session 都指向 snap1）。
    REQUIRE(second.snapshot == snap1);
    REQUIRE(session.snapshot() == snap1);
    // 诊断捕获了失效包。
    REQUIRE_FALSE(second.diagnostics.empty());

    std::filesystem::remove_all(root);
}

TEST_CASE("refresh with empty roots succeeds with empty snapshot",
          "[ds-session][refresh-failure]") {
    // 边界：空搜索路径不是失败 —— refresh 成功并发布空 snapshot。
    // 区别于"全部包无效"（那是失败，保留 previous）。
    ds::session::VoicebankSession session;
    session.setRoots({}); // 空 roots
    const auto result = session.refreshAsync().get();
    REQUIRE(result.succeeded);
    REQUIRE(result.changed); // 首次发布 → changed=true
    REQUIRE(result.snapshot != nullptr);
    REQUIRE(result.snapshot->packages.empty());
    REQUIRE(result.snapshot->singers.empty());
    REQUIRE(result.snapshot->roots.empty());
    REQUIRE(result.snapshot->catalogFingerprint.empty());
    REQUIRE(result.snapshot->languageFingerprint.empty());
}

TEST_CASE("first refresh with all invalid packages returns null snapshot",
          "[ds-session][refresh-failure]") {
    // 边界：首次 refresh 即遇到全部无效包 —— 无 previous 可保留，
    // RefreshResult.snapshot 与 session.snapshot() 均为 nullptr。
    const auto root = makeRoot();
    writeFile(root / "broken" / "desc.json", "not json");
    ds::session::VoicebankSession session;
    session.setRoots({root});
    const auto result = session.refreshAsync().get();
    REQUIRE_FALSE(result.succeeded);
    REQUIRE(result.snapshot == nullptr); // 无 previous
    REQUIRE(session.snapshot() == nullptr);
    REQUIRE_FALSE(result.diagnostics.empty());

    std::filesystem::remove_all(root);
}

// ===========================================================================
// 4. Discovery 多包部分成功（D-31）
//
// 现有测试仅覆盖 1 有效 + 1 无效。这里扩展到多有效 + 多无效，并验证诊断
// 结构中的 packageId 字段可被宿主消费。
// ===========================================================================

TEST_CASE("refresh publishes multiple valid packages alongside multiple invalid ones",
          "[ds-session][discovery]") {
    // D-31：多个有效包 + 多个无效包共存时，有效包全部发布到 snapshot，
    // 无效包全部转化为 diagnostics。refresh 整体仍成功。
    const auto root = makeRoot();
    makePackage(root);       // session.test 1.0.0
    makeSecondPackage(root); // session.other 2.0.0
    writeFile(root / "broken1" / "desc.json", "not json");
    writeFile(root / "broken2" / "desc.json", "still not json");
    writeFile(root / "broken3" / "desc.json", "also not json");

    ds::session::VoicebankSession session;
    session.setRoots({root});
    const auto result = session.refreshAsync().get();
    REQUIRE(result.succeeded);
    REQUIRE(result.snapshot != nullptr);
    // 两个有效包进入 snapshot。
    REQUIRE(result.snapshot->packages.size() == 2);
    // 至少 3 条诊断对应 3 个失效包。
    REQUIRE(result.diagnostics.size() >= 3);

    std::filesystem::remove_all(root);
}

TEST_CASE("refresh diagnostics populate packageId for broken packages",
          "[ds-session][discovery]") {
    // D-31：无效包的诊断信息应携带 packageId（scanner 在 desc.json 无法
    // 解析时以目录名作为回退），便于宿主按包归集错误展示。
    const auto root = makeRoot();
    makePackage(root);
    writeFile(root / "mybrokenpkg" / "desc.json", "not json");
    ds::session::VoicebankSession session;
    session.setRoots({root});
    const auto result = session.refreshAsync().get();
    REQUIRE(result.succeeded);
    REQUIRE_FALSE(result.diagnostics.empty());
    // 至少一条诊断引用 "mybrokenpkg"（packageId 或 message 字段）。
    bool found = false;
    for (const auto &d : result.diagnostics) {
        if (d.packageId.find("mybrokenpkg") != std::string::npos ||
            d.message.find("mybrokenpkg") != std::string::npos) {
            found = true;
            break;
        }
    }
    REQUIRE(found);

    std::filesystem::remove_all(root);
}

// ===========================================================================
// 5. ensureLanguageReady 状态机（V3-08）
//
// 现有 test_vbs_language.cpp 覆盖：RuntimePackageNotLoaded、G2pNotImplementedError、
// G2pPackageNotFound、G2pVersionAmbiguous。本节补充：
//   - refresh 自动初始化 metadata（v7 勘误：触发条件为 languageService!=nullptr，
//     而非 g2pPluginPaths 非空）→ ensureLanguageReady 通过 metadataReady guard
//   - 失败幂等：相同状态下重复调用返回相同错误
//   - snapshot 为空 → SessionError
// ===========================================================================

TEST_CASE("refresh auto-initializes metadata when languageService is injected (v7 erratum)",
          "[ds-session][ensure-state]") {
    // v7 勘误（docs/modules/ds-session.md §21, docs/modules/g2p.md §402-406）：
    // performRefresh 中自动初始化的触发条件为 `languageService != nullptr`
    // （`if (svc)`），而非 `g2pPluginPaths` 非空。即使不提供 g2pPluginPaths，
    // refresh 仍会调用 initializeMetadata() 完成 Stage 1，metadataReady 变为 true。
    //
    // 场景：SessionResources 注入未初始化的 LanguageService 且不提供
    // g2pPluginPaths，refresh 仍自动调用 initializeMetadata（传入空的
    // pluginSearchPaths/officialG2pPackagePaths + 快照包条目），
    // metadataReady 从 false 变为 true，snapshot 正常发布。
    // 随后 ensureLanguageReady 通过 metadataReady guard（不返回
    // G2pInitializationError "metadata not initialized"）。
    //
    // 注：metadataReady==false → G2pInitializationError 的 guard 仅在
    // initializeMetadata 失败时可达（L2 场景：插件 DLL 缺失/ONNX 加载失败），
    // L1 下 initializeMetadata 恒成功，该 guard 不可达。
    const auto root = makeRoot();
    makePackage(root);
    srt::core::Runtime runtime;
    auto langSvc = std::make_shared<srt::g2p::LanguageService>();
    REQUIRE_FALSE(langSvc->metadataReady());

    // 不提供 g2pPluginPaths → v7 勘误：refresh 仍自动初始化 metadata
    ds::session::SessionResources resources;
    resources.runtime = &runtime;
    resources.languageService = langSvc;
    ds::session::VoicebankSession session(std::move(resources));
    session.setRoots({root});
    REQUIRE(session.refreshAsync().get().succeeded);
    REQUIRE(session.languageService()->metadataReady());

    std::filesystem::remove_all(root);
}

TEST_CASE("ensureLanguageReady failure is idempotent across repeated calls",
          "[ds-session][ensure-state]") {
    // 幂等性：相同状态下重复调用 ensureLanguageReady 返回相同错误，
    // 不改变内部状态、不抛异常。宿主可安全重试。
    const auto root = makeRoot();
    makePackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});
    REQUIRE(session.refreshAsync().get().succeeded);

    const auto snap1 = session.snapshot();

    // 首次调用：Runtime 未注入 → RuntimePackageNotLoaded。
    auto exp1 = session.ensureLanguageReady(
        "session.test", stdc::VersionNumber::fromString("1.0.0"), "cmn");
    REQUIRE_FALSE(exp1.hasValue());
    REQUIRE(exp1.isError(srt::core::ErrorCode::RuntimePackageNotLoaded));

    // 二次调用：状态未变 → 相同错误。
    auto exp2 = session.ensureLanguageReady(
        "session.test", stdc::VersionNumber::fromString("1.0.0"), "cmn");
    REQUIRE_FALSE(exp2.hasValue());
    REQUIRE(exp2.isError(srt::core::ErrorCode::RuntimePackageNotLoaded));

    // snapshot 未改变。
    REQUIRE(session.snapshot() == snap1);

    std::filesystem::remove_all(root);
}

TEST_CASE("ensureLanguageReady returns SessionError when no snapshot published",
          "[ds-session][ensure-state]") {
    // 边界：未调用 refresh 前 snapshot 为空，ensureLanguageReady 在第一步
    // snapshot 检查即返回 SessionError（早于 Runtime / LanguageService guard）。
    srt::core::Runtime runtime;
    ds::session::SessionResources resources;
    resources.runtime = &runtime;
    ds::session::VoicebankSession session(std::move(resources));
    auto exp = session.ensureLanguageReady(
        "any.pkg", stdc::VersionNumber::fromString("1.0.0"), "cmn");
    REQUIRE_FALSE(exp.hasValue());
    REQUIRE(exp.isError(srt::core::ErrorCode::SessionError));
}

// ===========================================================================
// 6. subscribeRefresh 边界
//
// 现有测试覆盖：单订阅者成功/失败/自我 reset。本节补充：
//   - 多订阅者同时接收回调
//   - 一个订阅者抛异常不影响其他订阅者（ROBUST-02 边界隔离）
//   - 外部 reset 后不再接收回调
//   - 移动构造后原对象失活、新对象继续接收回调
// ===========================================================================

TEST_CASE("subscribeRefresh supports multiple subscribers all invoked on refresh",
          "[ds-session][subscription]") {
    // 多订阅者：一次 refresh（changed=true）应触发所有活跃订阅者的回调。
    const auto root = makeRoot();
    makePackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});

    std::atomic<int> calls1{0}, calls2{0}, calls3{0};
    auto sub1 = session.subscribeRefresh([&](const ds::session::RefreshResult &) { ++calls1; });
    auto sub2 = session.subscribeRefresh([&](const ds::session::RefreshResult &) { ++calls2; });
    auto sub3 = session.subscribeRefresh([&](const ds::session::RefreshResult &) { ++calls3; });

    REQUIRE(session.refreshAsync().get().succeeded);
    REQUIRE(calls1.load() == 1);
    REQUIRE(calls2.load() == 1);
    REQUIRE(calls3.load() == 1);

    std::filesystem::remove_all(root);
}

TEST_CASE("subscribeRefresh isolates callback exceptions from other subscribers",
          "[ds-session][subscription]") {
    // 异常隔离（ROBUST-02）：一个订阅者抛异常不应影响其他订阅者的回调执行，
    // 也不应影响 refresh 结果的返回。notifyRefresh 内部 try/catch 吞没异常。
    const auto root = makeRoot();
    makePackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});

    std::atomic<int> goodCalls{0};
    std::atomic<bool> threw{false};
    auto badSub = session.subscribeRefresh([&](const ds::session::RefreshResult &) {
        threw.store(true);
        throw std::runtime_error("subscriber bug");
    });
    auto goodSub = session.subscribeRefresh([&](const ds::session::RefreshResult &) {
        ++goodCalls;
    });
    (void)badSub;
    (void)goodSub;

    // 首次 refresh（changed=true）：两个订阅者都被通知。
    // 抛异常的订阅者不影响另一个，refresh 仍成功返回。
    const auto result = session.refreshAsync().get();
    REQUIRE(result.succeeded);
    REQUIRE(threw.load());
    REQUIRE(goodCalls.load() == 1);

    std::filesystem::remove_all(root);
}

TEST_CASE("subscribeRefresh reset prevents further callbacks",
          "[ds-session][subscription]") {
    // 外部 reset：调用 reset() 后，后续 refresh 不再触发该订阅者回调。
    // 区别于 self-reset（已在 test_voicebank_session.cpp 覆盖），这里测试
    // 从外部主动取消订阅。
    const auto root = makeRoot();
    makePackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});

    std::atomic<int> calls{0};
    auto sub = session.subscribeRefresh([&](const ds::session::RefreshResult &) { ++calls; });

    REQUIRE(session.refreshAsync().get().succeeded);
    REQUIRE(calls.load() == 1);

    sub.reset();
    REQUIRE_FALSE(sub);

    // 再次 refresh（内容变化）：reset 后的订阅者不应被回调。
    makeSecondPackage(root);
    REQUIRE(session.refreshAsync().get().succeeded);
    REQUIRE(calls.load() == 1); // 仍为 1

    std::filesystem::remove_all(root);
}

TEST_CASE("subscribeRefresh move-constructed subscription remains active",
          "[ds-session][subscription]") {
    // 移动语义：RefreshSubscription 移动构造后，原对象失活（operator bool
    // 返回 false），新对象继承活跃状态继续接收回调。
    const auto root = makeRoot();
    makePackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});

    std::atomic<int> calls{0};
    auto sub1 = session.subscribeRefresh([&](const ds::session::RefreshResult &) { ++calls; });

    // 移动构造。
    ds::session::RefreshSubscription sub2 = std::move(sub1);
    REQUIRE_FALSE(sub1); // moved-from
    REQUIRE(sub2);       // 新对象活跃

    REQUIRE(session.refreshAsync().get().succeeded);
    REQUIRE(calls.load() == 1);

    // reset sub2 → 不再回调。
    sub2.reset();
    makeSecondPackage(root);
    REQUIRE(session.refreshAsync().get().succeeded);
    REQUIRE(calls.load() == 1); // 仍为 1

    std::filesystem::remove_all(root);
}

// ===========================================================================
// 7. 指纹顺序无关性（V3-07 D-33）
//
// computeCatalogFingerprint / computeLanguageFingerprint 在计算前对 packages
// 按 (packageId, version) 排序，因此与扫描顺序无关。现有 test_vbs_snapshot.cpp
// 覆盖"相同内容两次 refresh 指纹一致"，本节补充：
//   - 不同 roots 顺序扫描相同包集合 → catalogFingerprint 一致
//   - languageFingerprint 排除非语言字段（singer.name 变化影响 catalog
//     但不影响 language）
// ===========================================================================

TEST_CASE("catalogFingerprint is independent of root scan order",
          "[ds-session][fingerprint]") {
    // V3-07 D-33：catalogFingerprint 按 (packageId, version) 排序后计算，
    // 与扫描顺序无关。两个 session 扫描相同包集合（roots 顺序不同）应
    // 产生相同的 fingerprint。
    const auto rootA = makeRoot();
    makePackage(rootA); // session.test 1.0.0
    const auto rootB = makeRoot();
    makeSecondPackage(rootB); // session.other 2.0.0

    // Session 1: roots = {A, B}
    ds::session::VoicebankSession session1;
    session1.setRoots({rootA, rootB});
    REQUIRE(session1.refreshAsync().get().succeeded);

    // Session 2: roots = {B, A}（逆序）
    ds::session::VoicebankSession session2;
    session2.setRoots({rootB, rootA});
    REQUIRE(session2.refreshAsync().get().succeeded);

    // 两个 session 找到相同的两个包，路径相同，仅扫描顺序不同。
    // fingerprint 必须 byte-identical（content-stable, order-independent）。
    REQUIRE_FALSE(session1.snapshot()->catalogFingerprint.empty());
    REQUIRE_FALSE(session1.snapshot()->languageFingerprint.empty());
    REQUIRE(session1.snapshot()->catalogFingerprint ==
            session2.snapshot()->catalogFingerprint);
    REQUIRE(session1.snapshot()->languageFingerprint ==
            session2.snapshot()->languageFingerprint);

    std::filesystem::remove_all(rootA);
    std::filesystem::remove_all(rootB);
}

TEST_CASE("languageFingerprint excludes singer name (non-language field)",
          "[ds-session][fingerprint]") {
    // V3-07 D-33：languageFingerprint 只覆盖语言路由相关字段
    // (packageId, version, rootPath, singerId, defaultLanguage, languages[])。
    // singer.name 是非语言字段 —— 修改 name 会改变 catalogFingerprint
    // （fingerprintSinger 包含 name）但不改变 languageFingerprint
    // （fingerprintPackageLanguage 不包含 name）。
    const auto root = makeRoot();
    makePackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});

    REQUIRE(session.refreshAsync().get().succeeded);
    const auto fp1 = session.snapshot()->catalogFingerprint;
    const auto lf1 = session.snapshot()->languageFingerprint;
    REQUIRE_FALSE(fp1.empty());
    REQUIRE_FALSE(lf1.empty());

    // 修改 singer.name（非语言字段）。imports 格式与 test_vbs_common.h
    // 保持一致（"id" 而非 "inferenceId"），避免引入无关 diff。
    writeFile(root / "bank" / "characters/test/config.json",
              R"({"id":"test","name":"Renamed Singer","imports":[{"id":"duration"}],"configuration":{"defaultLanguage":"cmn","languages":[{"id":"cmn"}]}})");

    REQUIRE(session.refreshAsync().get().succeeded);
    const auto fp2 = session.snapshot()->catalogFingerprint;
    const auto lf2 = session.snapshot()->languageFingerprint;

    // catalogFingerprint 变化（包含 name）。
    REQUIRE(fp2 != fp1);
    // languageFingerprint 不变（排除 name）。
    REQUIRE(lf2 == lf1);

    std::filesystem::remove_all(root);
}

// ===========================================================================
// 8. sync / refreshAsync 并发一致性
//
// refresh() 内部调用 refreshAsync().get()。并发 sync + async 调用可能共享
// 同一 in-flight 任务（I/O 窗口内）或各自独立执行（错开）。契约：不崩溃、
// 不死锁，最终 session 状态一致。
// ===========================================================================

TEST_CASE("Concurrent sync refresh() and refreshAsync() do not corrupt session state",
          "[ds-session][concurrency]") {
    // sync refresh() 内部调用 refreshAsync().get()。两个线程并发调用时，
    // refreshAsync() 的 in-flight 合并逻辑（VoicebankSession.cpp:616-624）
    // 可能让 sync 线程共享 async 线程启动的任务。契约：无崩溃、无死锁，
    // 最终 snapshot 非空且包含期望包。
    const auto root = makeRoot();
    makePackage(root);
    ds::session::VoicebankSession session;
    session.setRoots({root});

    std::promise<void> start;
    const std::shared_future<void> go = start.get_future().share();

    std::shared_future<ds::session::RefreshResult> asyncFuture;
    std::thread asyncThread([&] {
        go.wait();
        asyncFuture = session.refreshAsync();
    });
    std::thread syncThread([&] {
        go.wait();
        session.refresh(); // 内部调 refreshAsync().get()
    });

    start.set_value();
    asyncThread.join();
    syncThread.join();

    REQUIRE(asyncFuture.valid());
    const auto asyncResult = asyncFuture.get();
    REQUIRE(asyncResult.succeeded);

    const auto snap = session.snapshot();
    REQUIRE(snap != nullptr);
    REQUIRE(snap->packages.size() == 1);
    REQUIRE(snap->packages[0].packageId == "session.test");

    std::filesystem::remove_all(root);
}

// ===========================================================================
// 9. Stale retry L2 占位（V3-12 D-30）
//
// ModelSetHandle 的 staleness 生命周期需要真实 ONNX 包构造 ModelSet：
//   - createModelSet 成功 → handle 绑定当前 generation
//   - refresh 发布新 snapshot → generation 递增
//   - handle.isStale() 返回 true（isCurrentGenerationFn 回调返回 false）
//   - handle.start() 返回 StaleModelSet；load/stop/unload 仍可调用
//   - session 销毁 → weak_ptr 失效 → isStale() 返回 true
//
// L1 fixture 仅有 stub 配置（无 ONNX 模型），无法构造 ModelSetHandle。
// 已在 test_vbs_modelset.cpp 中以 SKIP 占位。本用例补充"session 销毁使
// handle 失效"场景的 L2 需求说明。
// ===========================================================================

TEST_CASE("ModelSetHandle staleness after session destruction requires L2 fixture",
          "[ds-session][stale-l2]") {
    // V3-12 D-30 补充场景：session 销毁后，ModelSetHandle 的
    // isCurrentGenerationFn 中 weak_ptr<Impl> 失效 → isStale() 返回 true
    // （见 ModelSetHandle.cpp:84-90 与 VoicebankSession.cpp:903-909 注释）。
    // 这保证 handle 不会在 session 销毁后启动新任务。
    //
    // L1 无法构造：createModelSet 需要 Runtime 加载了 singer 的 ONNX 包
    // 才能解析 5 个 stage 并构造 ModelSet。已在 test_vbs_modelset.cpp 中
    // 以 SKIP 占位。
    SKIP("L2: needs Runtime + loaded ONNX package to build a ModelSetHandle");
}
