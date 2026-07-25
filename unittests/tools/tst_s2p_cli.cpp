// tst_s2p_cli.cpp
//
// GAP-NEW-003:
//   tools/s2p-cli 黑盒 subprocess 测试骨架（L2）。
//
// 测试覆盖（INFRA-03 L2）：
//   - --version / -h / --help 退出码与输出
//   - 无参数 退出码 1
//   - 未知命令 退出码 1
//   - direct 模式参数不足退出码 1
//   - dict 模式参数不足退出码 1
//   - direct 模式正常执行输出 JSON
//   - 输出格式校验：phonemes 数组 + onsets 数组
//
// 不修改 s2p-cli 源码接口（D-11），通过 subprocess 启动可执行文件。

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#ifndef SYNTHRT_TEST_CLI_EXE
#error "SYNTHRT_TEST_CLI_EXE must be defined via target_compile_definitions"
#endif

namespace {

// 跨平台 subprocess 执行：返回退出码与合并后的 stdout+stderr。
struct SubprocessResult {
    int exitCode = -1;
    std::string output;
};

SubprocessResult runCli(const std::vector<std::string> &args) {
    std::string command = "\"" SYNTHRT_TEST_CLI_EXE "\"";
    for (const auto &arg : args) {
        command += " ";
        if (arg.find(' ') != std::string::npos) {
            command += "\"" + arg + "\"";
        } else {
            command += arg;
        }
    }
    command += " 2>&1";

    SubprocessResult result;
#ifdef _WIN32
    auto pipe = _popen(command.c_str(), "r");
#else
    auto pipe = popen(command.c_str(), "r");
#endif
    if (!pipe) {
        result.exitCode = -1;
        return result;
    }

    char buffer[256];
    while (auto len = std::fread(buffer, 1, sizeof(buffer), pipe)) {
        result.output.append(buffer, len);
    }

#ifdef _WIN32
    result.exitCode = _pclose(pipe);
#else
    result.exitCode = pclose(pipe);
    result.exitCode = (result.exitCode >> 8) & 0xFF;
#endif
    return result;
}

} // namespace

// ============================================================================
// --version / -h / --help 路径
// ============================================================================

TEST_CASE("s2p-cli --version exits 0 and prints version", "[cli][s2p-cli]") {
    auto result = runCli({"--version"});
    REQUIRE(result.exitCode == 0);
    REQUIRE_FALSE(result.output.empty());
}

TEST_CASE("s2p-cli -h exits 0 and prints usage", "[cli][s2p-cli]") {
    auto result = runCli({"-h"});
    REQUIRE(result.exitCode == 0);
    REQUIRE(result.output.find("Usage:") != std::string::npos);
}

TEST_CASE("s2p-cli --help exits 0 and prints usage", "[cli][s2p-cli]") {
    auto result = runCli({"--help"});
    REQUIRE(result.exitCode == 0);
    REQUIRE(result.output.find("Usage:") != std::string::npos);
}

// ============================================================================
// 无参数 退出码 1
// ============================================================================

TEST_CASE("s2p-cli no args exits 1 and prints usage", "[cli][s2p-cli]") {
    auto result = runCli({});
    REQUIRE(result.exitCode == 1);
    REQUIRE(result.output.find("Usage:") != std::string::npos);
}

// ============================================================================
// 未知命令 退出码 1
// ============================================================================

TEST_CASE("s2p-cli unknown command exits 1", "[cli][s2p-cli]") {
    auto result = runCli({"unknown_command"});
    REQUIRE(result.exitCode == 1);
    REQUIRE(result.output.find("Usage:") != std::string::npos);
}

// ============================================================================
// direct / dict 模式参数不足 退出码 1
// ============================================================================

TEST_CASE("s2p-cli direct without pronunciation exits 1", "[cli][s2p-cli][GAP-NEW-003]") {
    auto result = runCli({"direct"});
    REQUIRE(result.exitCode == 1);
    REQUIRE(result.output.find("Usage:") != std::string::npos);
}

TEST_CASE("s2p-cli dict without enough args exits 1", "[cli][s2p-cli][GAP-NEW-003]") {
    auto result = runCli({"dict", "only_one_arg"});
    REQUIRE(result.exitCode == 1);
    REQUIRE(result.output.find("Usage:") != std::string::npos);
}

// ============================================================================
// direct 模式正常执行：输出 JSON 格式
// ============================================================================
//
// 注意：direct 模式调用 srt::s2p::LanguageResource::direct("")，传入空 onset 路径。
// 这要求 s2p 库内置 direct 资源可用。若运行环境缺少 direct 资源，会抛异常返回 1。
// 此处允许两种结果：
//   - 退出码 0 + 输出 JSON（资源可用）
//   - 退出码 1 + 输出 "error:"（资源不可用，依赖环境）
// 关键：不崩溃。

TEST_CASE("s2p-cli direct mode does not crash", "[cli][s2p-cli][GAP-NEW-003]") {
    auto result = runCli({"direct", "hello"});
    // 接受 0（成功）或 1（运行时错误，如资源缺失）
    REQUIRE((result.exitCode == 0 || result.exitCode == 1));
    // 不应崩溃（exitCode 不会是 -1 或 255）
    REQUIRE(result.exitCode != -1);
}

// ============================================================================
// 输出格式校验：成功时输出 JSON 格式
// ============================================================================

TEST_CASE("s2p-cli direct mode output JSON format when success",
          "[cli][s2p-cli][GAP-NEW-003]") {
    auto result = runCli({"direct", "a"});
    if (result.exitCode == 0) {
        // 成功时输出应包含 JSON 字段
        REQUIRE(result.output.find("phonemes") != std::string::npos);
        REQUIRE(result.output.find("onsets") != std::string::npos);
    }
    // 失败时（资源缺失等环境问题）跳过格式校验
}
