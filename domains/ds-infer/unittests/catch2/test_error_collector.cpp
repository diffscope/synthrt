// ErrorCollector 单元测试：错误收集器行为。
//
// 覆盖范围：
//   - 默认构造：hasErrors() 返回 false，errors() 为空
//   - collectError(const char*) / collectError(const std::string&) / collectError(std::string&&)
//   - 多次 collectError 累积到 errors()
//   - getErrorMessage 在无错误时返回空字符串
//   - getErrorMessage 在单个错误时的格式（"prefix (1 error found):\n1. msg"）
//   - getErrorMessage 在多个错误时的格式（"prefix (N errors found):\n1. msg;\n2. msg"）
//   - clear 清空错误后 hasErrors() 返回 false
//   - 中文/UTF-8 错误消息处理
//   - 空错误消息处理
//   - 超长错误消息处理
//
// 这些用例反映 ds-editor-lite 在 G2P/S2P pipeline 中收集多个非致命错误
// 后统一上报的实际使用模式。ErrorCollector 用于在 InputParser 等组件中
// 收集多个 validation 错误，避免一次只报一个导致用户多次修复-重试。

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include <inferutil/ErrorCollector.h>

using namespace ds::infer::inferutil;

// ---------------------------------------------------------------------------
// 默认状态
// ---------------------------------------------------------------------------

TEST_CASE("ErrorCollector default has no errors", "[errorcollector]") {
    ErrorCollector collector;
    REQUIRE(!collector.hasErrors());
    REQUIRE(collector.errors().empty());
}

TEST_CASE("ErrorCollector default getErrorMessage returns empty", "[errorcollector]") {
    ErrorCollector collector;
    REQUIRE(collector.getErrorMessage("Prefix").empty());
}

TEST_CASE("ErrorCollector default getErrorMessage with empty prefix returns empty",
          "[errorcollector]") {
    ErrorCollector collector;
    REQUIRE(collector.getErrorMessage("").empty());
}

// ---------------------------------------------------------------------------
// collectError 重载
// ---------------------------------------------------------------------------

TEST_CASE("ErrorCollector collectError const char*", "[errorcollector]") {
    ErrorCollector collector;
    collector.collectError("error one");
    REQUIRE(collector.hasErrors());
    REQUIRE(collector.errors().size() == 1);
    REQUIRE(collector.errors()[0] == "error one");
}

TEST_CASE("ErrorCollector collectError const std::string&", "[errorcollector]") {
    ErrorCollector collector;
    std::string msg = "error two";
    collector.collectError(msg);
    REQUIRE(collector.hasErrors());
    REQUIRE(collector.errors().size() == 1);
    REQUIRE(collector.errors()[0] == "error two");
}

TEST_CASE("ErrorCollector collectError std::string&& moves", "[errorcollector]") {
    ErrorCollector collector;
    std::string msg = "error three";
    collector.collectError(std::move(msg));
    REQUIRE(collector.hasErrors());
    REQUIRE(collector.errors().size() == 1);
    REQUIRE(collector.errors()[0] == "error three");
}

TEST_CASE("ErrorCollector collectError empty string", "[errorcollector]") {
    ErrorCollector collector;
    collector.collectError("");
    REQUIRE(collector.hasErrors());
    REQUIRE(collector.errors().size() == 1);
    REQUIRE(collector.errors()[0].empty());
}

// ---------------------------------------------------------------------------
// 多错误累积
// ---------------------------------------------------------------------------

TEST_CASE("ErrorCollector accumulates multiple errors", "[errorcollector]") {
    ErrorCollector collector;
    collector.collectError("first");
    collector.collectError("second");
    collector.collectError("third");
    REQUIRE(collector.hasErrors());
    REQUIRE(collector.errors().size() == 3);
    REQUIRE(collector.errors()[0] == "first");
    REQUIRE(collector.errors()[1] == "second");
    REQUIRE(collector.errors()[2] == "third");
}

TEST_CASE("ErrorCollector collects mixed type errors", "[errorcollector]") {
    ErrorCollector collector;
    collector.collectError("const char*");
    std::string s = "std::string";
    collector.collectError(s);
    collector.collectError(std::move(s));
    REQUIRE(collector.errors().size() == 3);
    REQUIRE(collector.errors()[0] == "const char*");
    REQUIRE(collector.errors()[1] == "std::string");
    REQUIRE(collector.errors()[2] == "std::string");
}

// ---------------------------------------------------------------------------
// getErrorMessage 格式
// ---------------------------------------------------------------------------

TEST_CASE("ErrorCollector getErrorMessage single error format", "[errorcollector]") {
    ErrorCollector collector;
    collector.collectError("something went wrong");

    auto msg = collector.getErrorMessage("Validation");
    // 格式："Validation (1 error found):\n1. something went wrong"
    REQUIRE(msg.find("Validation") != std::string::npos);
    REQUIRE(msg.find("1 error found") != std::string::npos);
    REQUIRE(msg.find("1. something went wrong") != std::string::npos);
    // 单数形式 "error"，不是 "errors"
    REQUIRE(msg.find("errors found") == std::string::npos);
}

TEST_CASE("ErrorCollector getErrorMessage multiple errors format", "[errorcollector]") {
    ErrorCollector collector;
    collector.collectError("first error");
    collector.collectError("second error");

    auto msg = collector.getErrorMessage("Parse");
    // 格式："Parse (2 errors found):\n1. first error;\n2. second error"
    REQUIRE(msg.find("Parse") != std::string::npos);
    REQUIRE(msg.find("2 errors found") != std::string::npos);
    REQUIRE(msg.find("1. first error") != std::string::npos);
    REQUIRE(msg.find("2. second error") != std::string::npos);
    // 多个错误之间用 ";\n" 分隔
    REQUIRE(msg.find(";\n") != std::string::npos);
}

TEST_CASE("ErrorCollector getErrorMessage many errors numbered correctly",
          "[errorcollector]") {
    ErrorCollector collector;
    for (int i = 1; i <= 10; ++i) {
        collector.collectError("error " + std::to_string(i));
    }

    auto msg = collector.getErrorMessage("Multi");
    REQUIRE(msg.find("10 errors found") != std::string::npos);
    // 验证编号从 1 到 10
    for (int i = 1; i <= 10; ++i) {
        auto expected = std::to_string(i) + ". error " + std::to_string(i);
        REQUIRE(msg.find(expected) != std::string::npos);
    }
}

TEST_CASE("ErrorCollector getErrorMessage empty prefix", "[errorcollector]") {
    ErrorCollector collector;
    collector.collectError("err");
    auto msg = collector.getErrorMessage("");
    // 空 prefix 时仍应输出错误
    REQUIRE(!msg.empty());
    REQUIRE(msg.find("err") != std::string::npos);
    REQUIRE(msg.find("1 error found") != std::string::npos);
}

TEST_CASE("ErrorCollector getErrorMessage last error has no trailing semicolon",
          "[errorcollector]") {
    ErrorCollector collector;
    collector.collectError("first");
    collector.collectError("second");
    collector.collectError("third");

    auto msg = collector.getErrorMessage("P");
    // 最后一个错误后面不应有 ";\n"
    auto lastErrPos = msg.find("3. third");
    REQUIRE(lastErrPos != std::string::npos);
    auto afterLastErr = msg.substr(lastErrPos);
    REQUIRE(afterLastErr.find(";\n") == std::string::npos);
}

// ---------------------------------------------------------------------------
// clear
// ---------------------------------------------------------------------------

TEST_CASE("ErrorCollector clear empties errors", "[errorcollector]") {
    ErrorCollector collector;
    collector.collectError("a");
    collector.collectError("b");
    REQUIRE(collector.hasErrors());

    collector.clear();
    REQUIRE(!collector.hasErrors());
    REQUIRE(collector.errors().empty());
    REQUIRE(collector.getErrorMessage("P").empty());
}

TEST_CASE("ErrorCollector clear allows reuse", "[errorcollector]") {
    ErrorCollector collector;
    collector.collectError("first batch");
    collector.clear();
    collector.collectError("second batch");

    REQUIRE(collector.hasErrors());
    REQUIRE(collector.errors().size() == 1);
    REQUIRE(collector.errors()[0] == "second batch");
}

TEST_CASE("ErrorCollector clear on empty collector is safe", "[errorcollector]") {
    ErrorCollector collector;
    collector.clear(); // 不崩溃
    REQUIRE(!collector.hasErrors());
}

// ---------------------------------------------------------------------------
// UTF-8 / 中文错误消息
// ---------------------------------------------------------------------------

TEST_CASE("ErrorCollector handles UTF-8 Chinese error messages", "[errorcollector]") {
    ErrorCollector collector;
    collector.collectError("音素 token 未找到");
    collector.collectError("语言 ID 无效");

    REQUIRE(collector.errors().size() == 2);
    REQUIRE(collector.errors()[0] == "音素 token 未找到");
    REQUIRE(collector.errors()[1] == "语言 ID 无效");

    auto msg = collector.getErrorMessage("校验");
    REQUIRE(msg.find("校验") != std::string::npos);
    REQUIRE(msg.find("音素 token 未找到") != std::string::npos);
    REQUIRE(msg.find("语言 ID 无效") != std::string::npos);
}

// ---------------------------------------------------------------------------
// 超长错误消息
// ---------------------------------------------------------------------------

TEST_CASE("ErrorCollector handles very long error message", "[errorcollector]") {
    ErrorCollector collector;
    std::string longMsg(10000, 'x');
    collector.collectError(longMsg);

    REQUIRE(collector.errors().size() == 1);
    REQUIRE(collector.errors()[0].size() == 10000);

    auto msg = collector.getErrorMessage("P");
    REQUIRE(msg.find(longMsg) != std::string::npos);
}

TEST_CASE("ErrorCollector handles many errors", "[errorcollector]") {
    // 1000 个错误：验证 reserve 和性能
    ErrorCollector collector;
    for (int i = 0; i < 1000; ++i) {
        collector.collectError("error " + std::to_string(i));
    }
    REQUIRE(collector.errors().size() == 1000);

    auto msg = collector.getErrorMessage("Bulk");
    REQUIRE(msg.find("1000 errors found") != std::string::npos);
    REQUIRE(msg.find("1. error 0") != std::string::npos);
    REQUIRE(msg.find("1000. error 999") != std::string::npos);
}

// ---------------------------------------------------------------------------
// errors() 引用稳定性
// ---------------------------------------------------------------------------

TEST_CASE("ErrorCollector errors returns stable reference", "[errorcollector]") {
    ErrorCollector collector;
    collector.collectError("test");
    const auto &errors1 = collector.errors();
    const auto &errors2 = collector.errors();
    // 多次调用 errors() 应返回相同引用
    REQUIRE(&errors1 == &errors2);
    REQUIRE(errors1.size() == 1);
}

// ---------------------------------------------------------------------------
// 单个错误 vs 多个错误的语法差异（"error" vs "errors"）
// ---------------------------------------------------------------------------

TEST_CASE("ErrorCollector single error uses singular form", "[errorcollector]") {
    ErrorCollector collector;
    collector.collectError("only one");
    auto msg = collector.getErrorMessage("P");
    // 单数：1 error found
    REQUIRE(msg.find("1 error found") != std::string::npos);
    // 不应出现复数形式
    REQUIRE(msg.find("1 errors found") == std::string::npos);
}

TEST_CASE("ErrorCollector two errors uses plural form", "[errorcollector]") {
    ErrorCollector collector;
    collector.collectError("one");
    collector.collectError("two");
    auto msg = collector.getErrorMessage("P");
    // 复数：2 errors found
    REQUIRE(msg.find("2 errors found") != std::string::npos);
}
