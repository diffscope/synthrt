// tst_cli_log.cpp
//
// GAP-NEW-006: tools/dsinfer-cli/cli_log 边界用例单元测试（L1）。
//
// 测试覆盖：
//   - installLogCallback 幂等性（重复调用应为 no-op）
//   - uninstallLogCallback 清理与状态重置
//   - logG2pOutput 级别升级语义（TD-CLI-09）：
//       * failed=true → Critical
//       * mode==kG2pModeCopy → Warning
//       * 正常 → Debug
//   - logG2pInput / logS2pInput / logS2pOutput / logBuildWordsSummary 格式化
//
// 测试通过 Logger::setLogCallback 注入自定义捕获 callback，验证 cli_log.cpp
// 内部 cliLog.srtXxx 调用所使用的级别与消息内容。不直接调用匿名 namespace
// 中的 log_report_callback（D-11：不改源码）。

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <stdcorelib/support/versionnumber.h>

#include <synthrt/Core/Support/Logging.h>
#include <synthrt/G2P/Base/LangCommon.h>

#include "cli_log.h"

namespace {

    struct CapturedLog {
        int level = 0;
        std::string message;
        std::string category;

        void reset() {
            level = 0;
            message.clear();
            category.clear();
        }
    };

    CapturedLog g_captured;
    std::atomic<int> g_callCount{0};

    void captureCallback(int level, const srt::core::LogContext &ctx,
                         const std::string_view &msg) {
        g_captured.level = level;
        g_captured.message = std::string(msg);
        g_captured.category = ctx.category ? ctx.category : "";
        g_callCount.fetch_add(1, std::memory_order_relaxed);
    }

    int currentCallCount() {
        return g_callCount.load(std::memory_order_relaxed);
    }

    // RAII guard：保存原 callback，安装捕获 callback；析构时恢复。
    // 避免污染后续测试与全局 Logger 状态。
    struct LogCallbackGuard {
        srt::core::Logger::LogCallback prev = nullptr;

        LogCallbackGuard() : prev(srt::core::Logger::logCallback()) {
            g_captured.reset();
            g_callCount.store(0, std::memory_order_relaxed);
            srt::core::Logger::setLogCallback(captureCallback);
        }

        ~LogCallbackGuard() {
            srt::core::Logger::setLogCallback(prev);
        }
    };

} // namespace

// ============================================================================
// installLogCallback / uninstallLogCallback 幂等性与状态管理
// ============================================================================

TEST_CASE("installLogCallback is idempotent", "[dsinfer-cli][cli_log][GAP-NEW-006]") {
    // 先清空全局 callback，确保初始状态确定
    srt::core::Logger::setLogCallback(nullptr);
    REQUIRE(srt::core::Logger::logCallback() == nullptr);

    // 第一次安装：应设置 callback
    dsinfer_cli::installLogCallback();
    auto cb1 = srt::core::Logger::logCallback();
    REQUIRE(cb1 != nullptr);

    // 第二次安装：应为 no-op，callback 指针不变
    dsinfer_cli::installLogCallback();
    auto cb2 = srt::core::Logger::logCallback();
    REQUIRE(cb2 == cb1);

    // 第三次安装：仍应为 no-op
    dsinfer_cli::installLogCallback();
    REQUIRE(srt::core::Logger::logCallback() == cb1);

    // 清理
    dsinfer_cli::uninstallLogCallback();
    REQUIRE(srt::core::Logger::logCallback() == nullptr);
}

TEST_CASE("uninstallLogCallback resets installed state", "[dsinfer-cli][cli_log][GAP-NEW-006]") {
    dsinfer_cli::installLogCallback();
    REQUIRE(srt::core::Logger::logCallback() != nullptr);

    dsinfer_cli::uninstallLogCallback();
    REQUIRE(srt::core::Logger::logCallback() == nullptr);

    // 再次 install 应能成功（logCallbackInstalled 已重置为 false）
    dsinfer_cli::installLogCallback();
    REQUIRE(srt::core::Logger::logCallback() != nullptr);

    // 清理
    dsinfer_cli::uninstallLogCallback();
    REQUIRE(srt::core::Logger::logCallback() == nullptr);
}

TEST_CASE("uninstallLogCallback is safe when not installed",
          "[dsinfer-cli][cli_log][GAP-NEW-006]") {
    // 未安装时调用 uninstall 不应崩溃
    srt::core::Logger::setLogCallback(nullptr);
    dsinfer_cli::uninstallLogCallback();
    REQUIRE(srt::core::Logger::logCallback() == nullptr);
}

// ============================================================================
// logG2pOutput 级别升级语义（TD-CLI-09 P0-3 关键约束）
// ============================================================================

TEST_CASE("logG2pOutput emits Critical when failed=true",
          "[dsinfer-cli][cli_log][GAP-NEW-006]") {
    LogCallbackGuard guard;
    dsinfer_cli::logG2pOutput("a", "some_mode", true);
    REQUIRE(currentCallCount() == 1);
    REQUIRE(g_captured.level == srt::core::Logger::Critical);
    REQUIRE(g_captured.message.find("[FAILED]") != std::string::npos);
    REQUIRE(g_captured.message.find("a") != std::string::npos);
}

TEST_CASE("logG2pOutput emits Warning on copy fallback",
          "[dsinfer-cli][cli_log][GAP-NEW-006]") {
    LogCallbackGuard guard;
    dsinfer_cli::logG2pOutput("a", srt::g2p::kG2pModeCopy, false);
    REQUIRE(currentCallCount() == 1);
    REQUIRE(g_captured.level == srt::core::Logger::Warning);
    REQUIRE(g_captured.message.find("[copy fallback]") != std::string::npos);
}

TEST_CASE("logG2pOutput emits Debug on normal conversion",
          "[dsinfer-cli][cli_log][GAP-NEW-006]") {
    LogCallbackGuard guard;
    dsinfer_cli::logG2pOutput("a", "convert", false);
    REQUIRE(currentCallCount() == 1);
    REQUIRE(g_captured.level == srt::core::Logger::Debug);
    REQUIRE(g_captured.message.find("a") != std::string::npos);
    // 不应包含 failed / copy fallback 标记
    REQUIRE(g_captured.message.find("FAILED") == std::string::npos);
    REQUIRE(g_captured.message.find("copy fallback") == std::string::npos);
}

TEST_CASE("logG2pOutput failed=true takes precedence over copy mode",
          "[dsinfer-cli][cli_log][GAP-NEW-006]") {
    // failed=true 应优先于 mode==copy，最终级别为 Critical
    LogCallbackGuard guard;
    dsinfer_cli::logG2pOutput("a", srt::g2p::kG2pModeCopy, true);
    REQUIRE(currentCallCount() == 1);
    REQUIRE(g_captured.level == srt::core::Logger::Critical);
    REQUIRE(g_captured.message.find("[FAILED]") != std::string::npos);
}

// ============================================================================
// logG2pInput / logS2pInput 字段格式化
// ============================================================================

TEST_CASE("logG2pInput emits Debug with all fields",
          "[dsinfer-cli][cli_log][GAP-NEW-006]") {
    LogCallbackGuard guard;
    stdc::VersionNumber version(1, 2, 3);
    dsinfer_cli::logG2pInput("hello", "g2p_id", "ctx", version);
    REQUIRE(currentCallCount() == 1);
    REQUIRE(g_captured.level == srt::core::Logger::Debug);
    REQUIRE(g_captured.message.find("hello") != std::string::npos);
    REQUIRE(g_captured.message.find("g2p_id") != std::string::npos);
    REQUIRE(g_captured.message.find("ctx") != std::string::npos);
    // VersionNumber toString() 输出 "1.2.3"
    REQUIRE(g_captured.message.find("1.2.3") != std::string::npos);
}

TEST_CASE("logS2pInput emits Debug with pronunciation and mode",
          "[dsinfer-cli][cli_log][GAP-NEW-006]") {
    LogCallbackGuard guard;
    dsinfer_cli::logS2pInput("pron", "dict_mode", std::filesystem::path("c:/foo/bar.tsv"));
    REQUIRE(currentCallCount() == 1);
    REQUIRE(g_captured.level == srt::core::Logger::Debug);
    REQUIRE(g_captured.message.find("pron") != std::string::npos);
    REQUIRE(g_captured.message.find("dict_mode") != std::string::npos);
    // path::to_utf8 输出包含文件名
    REQUIRE(g_captured.message.find("bar.tsv") != std::string::npos);
}

// ============================================================================
// logS2pOutput phonemes + onset 标记格式化
// ============================================================================

TEST_CASE("logS2pOutput formats phonemes with onset marker",
          "[dsinfer-cli][cli_log][GAP-NEW-006]") {
    LogCallbackGuard guard;
    std::vector<std::string> phonemes = {"a", "b", "c"};
    std::vector<bool> onsets = {true, false, true};
    dsinfer_cli::logS2pOutput(phonemes, onsets);
    REQUIRE(currentCallCount() == 1);
    REQUIRE(g_captured.level == srt::core::Logger::Debug);
    // 期望 "a* b c*"
    REQUIRE(g_captured.message.find("a* b c*") != std::string::npos);
}

TEST_CASE("logS2pOutput handles empty phonemes",
          "[dsinfer-cli][cli_log][GAP-NEW-006]") {
    LogCallbackGuard guard;
    std::vector<std::string> phonemes;
    std::vector<bool> onsets;
    dsinfer_cli::logS2pOutput(phonemes, onsets);
    REQUIRE(currentCallCount() == 1);
    REQUIRE(g_captured.level == srt::core::Logger::Debug);
}

TEST_CASE("logS2pOutput handles shorter onsets vector",
          "[dsinfer-cli][cli_log][GAP-NEW-006]") {
    // onsets.size() < phonemes.size()：不应越界访问
    LogCallbackGuard guard;
    std::vector<std::string> phonemes = {"a", "b", "c"};
    std::vector<bool> onsets = {true}; // 仅 1 个
    dsinfer_cli::logS2pOutput(phonemes, onsets);
    REQUIRE(currentCallCount() == 1);
    // 仅 a 有 * 标记，b/c 无
    REQUIRE(g_captured.message.find("a* b c") != std::string::npos);
}

TEST_CASE("logS2pOutput handles empty onsets vector",
          "[dsinfer-cli][cli_log][GAP-NEW-006]") {
    // onsets 为空：所有 phonemes 均无 * 标记
    LogCallbackGuard guard;
    std::vector<std::string> phonemes = {"a", "b"};
    std::vector<bool> onsets;
    dsinfer_cli::logS2pOutput(phonemes, onsets);
    REQUIRE(currentCallCount() == 1);
    REQUIRE(g_captured.message.find("a b") != std::string::npos);
    // 不应包含 *
    REQUIRE(g_captured.message.find("*") == std::string::npos);
}

// ============================================================================
// logBuildWordsSummary 格式化
// ============================================================================

TEST_CASE("logBuildWordsSummary formats word and note counts",
          "[dsinfer-cli][cli_log][GAP-NEW-006]") {
    LogCallbackGuard guard;
    dsinfer_cli::logBuildWordsSummary(3, 5);
    REQUIRE(currentCallCount() == 1);
    REQUIRE(g_captured.level == srt::core::Logger::Debug);
    REQUIRE(g_captured.message.find("3 words from 5 notes") != std::string::npos);
}

TEST_CASE("logBuildWordsSummary handles zero counts",
          "[dsinfer-cli][cli_log][GAP-NEW-006]") {
    LogCallbackGuard guard;
    dsinfer_cli::logBuildWordsSummary(0, 0);
    REQUIRE(currentCallCount() == 1);
    REQUIRE(g_captured.message.find("0 words from 0 notes") != std::string::npos);
}

TEST_CASE("logBuildWordsSummary handles large counts",
          "[dsinfer-cli][cli_log][GAP-NEW-006]") {
    LogCallbackGuard guard;
    dsinfer_cli::logBuildWordsSummary(1000000, 5000000);
    REQUIRE(currentCallCount() == 1);
    REQUIRE(g_captured.message.find("1000000 words from 5000000 notes") != std::string::npos);
}
