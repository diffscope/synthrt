// tst_dsinfer_cli.cpp
//
// TD-CLI-02 + GAP-NEW-001 + GAP-NEW-005:
//   tools/dsinfer-cli 黑盒 subprocess 测试骨架（L2）。
//
// 测试覆盖（INFRA-03 L2）：
//   - --version / -h / --help 退出码与输出
//   - 无参数 / 参数不足 / 错误 mode 退出码
//   - CliArgs 错误路径（parseEp / parseDeviceIndex / --g2p-packages 无值）
//   - BUG-CLI-004/005/006 修复后的回归保护
//
// 不修改 dsinfer-cli 源码接口（D-11），通过 subprocess 启动可执行文件。

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
// 使用 _popen (Win) / popen (POSIX) 读取管道输出。
struct SubprocessResult {
    int exitCode = -1;
    std::string output;
};

SubprocessResult runCli(const std::vector<std::string> &args) {
    std::string command = "\"" SYNTHRT_TEST_CLI_EXE "\"";
    for (const auto &arg : args) {
        command += " ";
        // 简单转义：包含空格的参数加引号
        if (arg.find(' ') != std::string::npos) {
            command += "\"" + arg + "\"";
        } else {
            command += arg;
        }
    }
    command += " 2>&1";  // 合并 stderr 到 stdout

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
    // Windows _pclose 返回值即退出码
#else
    result.exitCode = pclose(pipe);
    // POSIX pclose 返回值需 WEXITSTATUS 提取（简化：直接返回值低 8 位）
    result.exitCode = (result.exitCode >> 8) & 0xFF;
#endif
    return result;
}

} // namespace

// ============================================================================
// --version / -h / --help 路径
// ============================================================================

TEST_CASE("dsinfer-cli --version exits 0 and prints version", "[cli][dsinfer-cli]") {
    auto result = runCli({"--version"});
    REQUIRE(result.exitCode == 0);
    REQUIRE_FALSE(result.output.empty());
    // TOOL_VERSION = "0.0.2.5"（CMakeLists.txt 定义）
    REQUIRE(result.output.find("0.0.2") != std::string::npos);
}

TEST_CASE("dsinfer-cli -h exits 0 and prints usage", "[cli][dsinfer-cli]") {
    auto result = runCli({"-h"});
    REQUIRE(result.exitCode == 0);
    REQUIRE(result.output.find("Usage:") != std::string::npos);
    // BUG-CLI-006 修复后 [max_segments] 不再出现
    REQUIRE(result.output.find("max_segments") == std::string::npos);
}

TEST_CASE("dsinfer-cli --help exits 0 and prints usage", "[cli][dsinfer-cli]") {
    auto result = runCli({"--help"});
    REQUIRE(result.exitCode == 0);
    REQUIRE(result.output.find("Usage:") != std::string::npos);
}

// ============================================================================
// 无参数 / 参数不足 退出码 1
// ============================================================================

TEST_CASE("dsinfer-cli no args exits 1 and prints usage", "[cli][dsinfer-cli]") {
    auto result = runCli({});
    REQUIRE(result.exitCode == 1);
    REQUIRE(result.output.find("Usage:") != std::string::npos);
}

TEST_CASE("dsinfer-cli insufficient positionals exits 1", "[cli][dsinfer-cli]") {
    // mode + packageDir + input + spk + output_dir (5 required)
    // 仅 4 个 → 退出码 1
    auto result = runCli({"midi", "pkg", "input.mid", "spk"});
    REQUIRE(result.exitCode == 1);
    REQUIRE(result.output.find("Usage:") != std::string::npos);
}

// ============================================================================
// 错误 mode 退出码 1（GAP-NEW-005: CliArgs 错误路径）
// ============================================================================

TEST_CASE("dsinfer-cli unknown mode exits 1", "[cli][dsinfer-cli][GAP-NEW-005]") {
    auto result = runCli({"unknown_mode", "pkg", "input.mid", "spk", "out"});
    REQUIRE(result.exitCode == 1);
    REQUIRE(result.output.find("unknown mode") != std::string::npos);
}

// ============================================================================
// CliArgs 错误路径（GAP-NEW-005）
// ============================================================================

TEST_CASE("dsinfer-cli invalid ep exits 1", "[cli][dsinfer-cli][GAP-NEW-005]") {
    // BUG-CLI-004/005: parseEp 失败应退出 1，而非静默回退 CPU
    auto result = runCli({"midi", "pkg", "input.mid", "spk", "out", "cmn", "invalid_ep"});
    REQUIRE(result.exitCode == 1);
    REQUIRE(result.output.find("error:") != std::string::npos);
}

TEST_CASE("dsinfer-cli invalid device index exits 1", "[cli][dsinfer-cli][GAP-NEW-005]") {
    // BUG-CLI-004: parseDeviceIndex 失败应退出 1
    auto result = runCli({"midi", "pkg", "input.mid", "spk", "out", "cmn", "cpu", "not_a_number"});
    REQUIRE(result.exitCode == 1);
    REQUIRE(result.output.find("error:") != std::string::npos);
}

TEST_CASE("dsinfer-cli --g2p-packages without value exits 1", "[cli][dsinfer-cli][GAP-NEW-005]") {
    auto result = runCli({"midi", "pkg", "input.mid", "spk", "out", "--g2p-packages"});
    REQUIRE(result.exitCode == 1);
    REQUIRE(result.output.find("Usage:") != std::string::npos);
}

TEST_CASE("dsinfer-cli --plugin-paths without value exits 1", "[cli][dsinfer-cli][GAP-NEW-005]") {
    auto result = runCli({"midi", "pkg", "input.mid", "spk", "out", "--plugin-paths"});
    REQUIRE(result.exitCode == 1);
    REQUIRE(result.output.find("Usage:") != std::string::npos);
}

// ============================================================================
// BUG-CLI-006 回归保护：max_segments 已删除，传入应被忽略或报错
// ============================================================================

TEST_CASE("dsinfer-cli BUG-CLI-006 max_segments positional ignored", "[cli][dsinfer-cli][BUG-CLI-006]") {
    // 9th positional 是原 max_segments 位置；删除后 parse 不再消费它
    // 传入应不报错（参数被 positionals 收集但不被使用）
    // 这里只校验 CLI 不会因额外位置参数崩溃
    //
    // 使用 --plugin-root 指向不存在的目录，使 execPipeline 在 ONNX driver
    // setup 阶段快速失败（return -1），避免在 build tree 中找到真实插件后
    // 触发完整 pipeline（加载 ONNX 模型等）导致 ctest 超时。
    // 测试关注点是 CLI 参数解析，而非 pipeline 执行。
    auto result = runCli({"midi", "pkg", "input.mid", "spk", "out", "cmn", "cpu", "0", "5",
                          "--plugin-root", "nonexistent_plugin_root"});
    // execPipeline 在 ONNX driver setup 失败时 return -1（Windows _pclose 也返回 -1）
    // 关键：不应是 exitCode 1（参数解析失败）
    REQUIRE(result.exitCode != 1);
}

// ============================================================================
// TD-CLI-10: dspx 模式条件编译路径（DSINFER_CLI_HAS_OPENDSPX）覆盖
// ============================================================================
//
// 测试运行时无法直接知道 DSINFER_CLI_HAS_OPENDSPX 是否定义，但可校验：
//   - dspx 模式 + 不存在的 .dspx 文件不会崩溃
//   - 退出码非 0（解析失败或 DSPX 未启用）
//   - 输出不应包含 "Usage:"（说明进入了 execPipeline 而非参数解析失败）
//
// 完整双路径覆盖需要 CI 中同时跑"定义"与"未定义"两种构建配置，
// 这里仅做黑盒层面的崩溃保护。
//
// 注意（2026-07-25 修正）：
//   当 opendspx 未启用时，main.cpp 调用 `return -1;`。Windows C 运行时
//   将该返回值直接传给 ExitProcess，_pclose 因此返回 -1。早先注释中
//   "Windows 转为 255" 的说法不准确（255 仅在调用 exit(255) 或 return 255
//   时才会出现）。因此 -1 是合法的"未启用 DSPX"路径退出码，不能视为崩溃。
//   判定崩溃的可靠依据是 output 为空且 exitCode == -1（_popen 失败）。

TEST_CASE("dsinfer-cli dspx mode does not crash on missing file",
          "[cli][dsinfer-cli][TD-CLI-10]") {
    // 使用 --plugin-root 指向不存在的目录，避免在 build tree 中找到真实
    // 插件后触发完整 pipeline（加载 ONNX 模型等）导致 ctest 超时。
    // 测试关注点是 dspx 模式的参数解析与不崩溃保护，而非 pipeline 执行。
    auto result = runCli({"dspx", "pkg", "nonexistent.dspx", "spk", "out",
                          "--plugin-root", "nonexistent_plugin_root"});

    // 1) 必须产生输出（_popen 失败时 output 为空且 exitCode=-1，视作崩溃）。
    //    只要 output 非空，就说明子进程启动并打印了诊断信息。
    REQUIRE_FALSE(result.output.empty());

    // 2) 不应是参数解析失败：5 个位置参数已满足，输出不应是 Usage。
    REQUIRE(result.output.find("Usage:") == std::string::npos);

    // 3) 退出码非 0：opendspx 未启用时 main 返回 -1（_pclose 也返回 -1）；
    //    opendspx 启用时 parseDspx 失败返回非 0 错误码。两种路径均非 0。
    //    另外，--plugin-root 不存在时 ONNX driver setup 也会 return -1。
    REQUIRE(result.exitCode != 0);
}
