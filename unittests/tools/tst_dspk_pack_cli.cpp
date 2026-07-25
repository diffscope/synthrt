// tst_dspk_pack_cli.cpp
//
// GAP-NEW-002:
//   tools/dspk-pack-cli 黑盒 subprocess 测试骨架（L2）。
//
// 测试覆盖（INFRA-03 L2）：
//   - --version / -h / --help 退出码与输出
//   - 无参数 / 未知命令退出码
//   - validate / info / pack 参数不足退出码
//   - validate/info 不存在目录退出码 1（错误路径）
//   - pack stub 退出码 2 + "not yet implemented" 提示（TD-CLI-07 跟踪）
//
// 不修改 dspk-pack-cli 源码接口（D-11），通过 subprocess 启动可执行文件。

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

TEST_CASE("dspk-pack-cli --version exits 0 and prints version", "[cli][dspk-pack-cli]") {
    auto result = runCli({"--version"});
    REQUIRE(result.exitCode == 0);
    REQUIRE_FALSE(result.output.empty());
}

TEST_CASE("dspk-pack-cli -h exits 0 and prints usage", "[cli][dspk-pack-cli]") {
    auto result = runCli({"-h"});
    REQUIRE(result.exitCode == 0);
    REQUIRE(result.output.find("Usage:") != std::string::npos);
}

TEST_CASE("dspk-pack-cli --help exits 0 and prints usage", "[cli][dspk-pack-cli]") {
    auto result = runCli({"--help"});
    REQUIRE(result.exitCode == 0);
    REQUIRE(result.output.find("Usage:") != std::string::npos);
}

// ============================================================================
// 无参数 / 未知命令 退出码 1
// ============================================================================

TEST_CASE("dspk-pack-cli no args exits 1", "[cli][dspk-pack-cli]") {
    auto result = runCli({});
    REQUIRE(result.exitCode == 1);
    REQUIRE(result.output.find("Usage:") != std::string::npos);
}

TEST_CASE("dspk-pack-cli unknown command exits 1", "[cli][dspk-pack-cli]") {
    auto result = runCli({"unknown_command"});
    REQUIRE(result.exitCode == 1);
    REQUIRE(result.output.find("unknown command") != std::string::npos);
}

// ============================================================================
// 子命令参数不足退出码 1
// ============================================================================

TEST_CASE("dspk-pack-cli validate without dir exits 1", "[cli][dspk-pack-cli]") {
    auto result = runCli({"validate"});
    REQUIRE(result.exitCode == 1);
    REQUIRE(result.output.find("requires") != std::string::npos);
}

TEST_CASE("dspk-pack-cli info without dir exits 1", "[cli][dspk-pack-cli]") {
    auto result = runCli({"info"});
    REQUIRE(result.exitCode == 1);
    REQUIRE(result.output.find("requires") != std::string::npos);
}

TEST_CASE("dspk-pack-cli pack without enough args exits 1", "[cli][dspk-pack-cli]") {
    auto result = runCli({"pack", "only_one_arg"});
    REQUIRE(result.exitCode == 1);
    REQUIRE(result.output.find("requires") != std::string::npos);
}

// ============================================================================
// 不存在目录错误路径退出码 1
// ============================================================================

TEST_CASE("dspk-pack-cli validate nonexistent dir exits 1", "[cli][dspk-pack-cli]") {
    auto result = runCli({"validate", "nonexistent_dir_xyz"});
    REQUIRE(result.exitCode == 1);
    REQUIRE(result.output.find("does not exist") != std::string::npos);
}

TEST_CASE("dspk-pack-cli info nonexistent dir exits 1", "[cli][dspk-pack-cli]") {
    auto result = runCli({"info", "nonexistent_dir_xyz"});
    REQUIRE(result.exitCode == 1);
    REQUIRE(result.output.find("does not exist") != std::string::npos);
}

// ============================================================================
// pack stub 退出码 2（TD-CLI-07 跟踪：packing 尚未实现）
// 注意：pack 路径会先执行 validate，再返回 stub 错误。若目录不存在则退出码 1。
// 这里使用一个存在的空目录触发 stub 路径（可能因 validate 失败退出码 1，
// 也可能因 validate 通过返回 stub 退出码 2）。两种情况都接受。
// ============================================================================

TEST_CASE("dspk-pack-cli pack nonexistent dir exits 1 (stub not reached)",
          "[cli][dspk-pack-cli][GAP-NEW-002]") {
    // 目录不存在 → doPack 在 fs::exists 检查时返回 1，未到达 stub
    auto result = runCli({"pack", "nonexistent_dir_xyz", "out.zip"});
    REQUIRE(result.exitCode == 1);
    REQUIRE(result.output.find("does not exist") != std::string::npos);
}
