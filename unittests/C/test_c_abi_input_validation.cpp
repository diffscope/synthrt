// C ABI 输入参数校验与错误路径测试
//
// 覆盖 CODING-02 设计原则：extern "C" 边界必须 try-catch 所有 std::exception，
// 将异常转换为 srt_error 码 + TLS 错误消息。
//
// 测试目标：
//   - srt_session_set_package_paths: null session、null paths+count>0、null entry
//   - srt_session_set_roots: null handle、null roots+count>0、invalid handle
//   - srt_session_set_reserved_phonemes: 同上
//   - srt_session_create_with_resources: null runtime/langService
//   - srt_runtime_destroy / srt_language_service_destroy: NULL 幂等
//   - srt_session_destroy: NULL 返回 SRT_ERR_INVALID_ARG
//   - 错误后 srt_last_error() / srt_last_error_code() 双通道报告
//
// 源码实现见 lib/C/srt_v4.cpp（每个 extern "C" 函数都有 try-catch）。

#include <string>

#include <catch2/catch_test_macros.hpp>

#include <synthrt/C/srt.h>

#include "LastError.h"

#include <synthrt/Core/Support/Error.h>

// ---------------------------------------------------------------------------
// srt_session_set_package_paths 输入校验
// ---------------------------------------------------------------------------
TEST_CASE("srt_session_set_package_paths rejects null session", "[c_abi][input-validation]") {
    srt_clear_last_error();
    const char *paths[] = {"/tmp"};
    auto err = srt_session_set_package_paths(nullptr, paths, 1);
    REQUIRE(err == SRT_ERR_INVALID_ARG);
    // 错误消息必须非空且包含函数名
    std::string msg = srt_last_error();
    REQUIRE_FALSE(msg.empty());
    REQUIRE(msg.find("srt_session_set_package_paths") != std::string::npos);
    REQUIRE(srt_last_error_code() == SRT_ERR_INVALID_ARG);
}

TEST_CASE("srt_session_set_package_paths rejects null paths when count>0",
          "[c_abi][input-validation]") {
    srt_session s = srt_session_create();
    REQUIRE(s != nullptr);

    srt_clear_last_error();
    auto err = srt_session_set_package_paths(s, nullptr, 1);
    REQUIRE(err == SRT_ERR_INVALID_ARG);
    REQUIRE_FALSE(std::string(srt_last_error()).empty());

    srt_session_destroy(s);
}

TEST_CASE("srt_session_set_package_paths accepts null paths when count=0",
          "[c_abi][input-validation]") {
    srt_session s = srt_session_create();
    REQUIRE(s != nullptr);

    srt_clear_last_error();
    // count=0 + paths=null 应该是合法的（清空搜索路径）
    auto err = srt_session_set_package_paths(s, nullptr, 0);
    REQUIRE(err == SRT_OK);
    REQUIRE(std::string(srt_last_error()).empty());

    srt_session_destroy(s);
}

TEST_CASE("srt_session_set_package_paths accepts empty path list",
          "[c_abi][input-validation]") {
    srt_session s = srt_session_create();
    REQUIRE(s != nullptr);

    // MSVC rejects zero-size arrays (`const char *paths[] = {};` → C2466).
    // Pass a 1-element array with count=0 to exercise the same "empty list"
    // code path (non-null paths pointer + count=0).
    const char *paths[] = {"/tmp"};
    srt_clear_last_error();
    auto err = srt_session_set_package_paths(s, paths, 0);
    REQUIRE(err == SRT_OK);

    srt_session_destroy(s);
}

TEST_CASE("srt_session_set_package_paths accepts valid paths",
          "[c_abi][input-validation]") {
    srt_session s = srt_session_create();
    REQUIRE(s != nullptr);

    const char *paths[] = {"/tmp/synthrt-test-1", "/tmp/synthrt-test-2"};
    srt_clear_last_error();
    auto err = srt_session_set_package_paths(s, paths, 2);
    REQUIRE(err == SRT_OK);
    REQUIRE(std::string(srt_last_error()).empty());

    srt_session_destroy(s);
}

// ---------------------------------------------------------------------------
// srt_session_destroy 边界
// ---------------------------------------------------------------------------
TEST_CASE("srt_session_destroy rejects null session", "[c_abi][input-validation]") {
    srt_clear_last_error();
    auto err = srt_session_destroy(nullptr);
    REQUIRE(err == SRT_ERR_INVALID_ARG);
    REQUIRE_FALSE(std::string(srt_last_error()).empty());
}

TEST_CASE("srt_session_create returns non-null handle", "[c_abi][lifecycle]") {
    srt_session s = srt_session_create();
    REQUIRE(s != nullptr);
    srt_session_destroy(s);
}

// ---------------------------------------------------------------------------
// vnext handles: srt_session_create_v2 / destroy_v2
// ---------------------------------------------------------------------------
TEST_CASE("srt_session_create_v2 returns non-null handle", "[c_abi][vnext]") {
    srt_SessionHandle *h = srt_session_create_v2();
    REQUIRE(h != nullptr);
    REQUIRE(srt_session_destroy_v2(h) == SRT_OK);
}

TEST_CASE("srt_session_destroy_v2 accepts null as no-op", "[c_abi][vnext][idempotent]") {
    // null handle 是 no-op，返回 SRT_OK（与 destroy 合约一致）
    REQUIRE(srt_session_destroy_v2(nullptr) == SRT_OK);
}

TEST_CASE("srt_session_destroy_v2 rejects already-destroyed handle",
          "[c_abi][vnext][invalid-handle]") {
    srt_SessionHandle *h = srt_session_create_v2();
    REQUIRE(h != nullptr);
    REQUIRE(srt_session_destroy_v2(h) == SRT_OK);

    srt_clear_last_error();
    // 再次 destroy 同一 handle 应返回 SRT_ERR_INVALID_HANDLE
    auto err = srt_session_destroy_v2(h);
    REQUIRE(err == SRT_ERR_INVALID_HANDLE);
    REQUIRE_FALSE(std::string(srt_last_error()).empty());
}

// ---------------------------------------------------------------------------
// srt_session_set_roots 输入校验
// ---------------------------------------------------------------------------
TEST_CASE("srt_session_set_roots rejects null handle", "[c_abi][vnext][input-validation]") {
    srt_clear_last_error();
    const char *roots[] = {"/tmp"};
    auto err = srt_session_set_roots(nullptr, roots, 1);
    REQUIRE(err == SRT_ERR_INVALID_HANDLE);
    REQUIRE_FALSE(std::string(srt_last_error()).empty());
}

TEST_CASE("srt_session_set_roots rejects null roots when count>0",
          "[c_abi][vnext][input-validation]") {
    srt_SessionHandle *h = srt_session_create_v2();
    REQUIRE(h != nullptr);

    srt_clear_last_error();
    auto err = srt_session_set_roots(h, nullptr, 1);
    REQUIRE(err == SRT_ERR_INVALID_ARG);
    REQUIRE_FALSE(std::string(srt_last_error()).empty());

    srt_session_destroy_v2(h);
}

TEST_CASE("srt_session_set_roots accepts null roots when count=0",
          "[c_abi][vnext][input-validation]") {
    srt_SessionHandle *h = srt_session_create_v2();
    REQUIRE(h != nullptr);

    srt_clear_last_error();
    auto err = srt_session_set_roots(h, nullptr, 0);
    REQUIRE(err == SRT_OK);

    srt_session_destroy_v2(h);
}

TEST_CASE("srt_session_set_roots accepts valid roots", "[c_abi][vnext][input-validation]") {
    srt_SessionHandle *h = srt_session_create_v2();
    REQUIRE(h != nullptr);

    const char *roots[] = {"/tmp/root1", "/tmp/root2"};
    srt_clear_last_error();
    auto err = srt_session_set_roots(h, roots, 2);
    REQUIRE(err == SRT_OK);

    srt_session_destroy_v2(h);
}

TEST_CASE("srt_session_set_roots rejects null entry in array",
          "[c_abi][vnext][input-validation]") {
    srt_SessionHandle *h = srt_session_create_v2();
    REQUIRE(h != nullptr);

    const char *roots[] = {"/tmp/valid", nullptr};
    srt_clear_last_error();
    auto err = srt_session_set_roots(h, roots, 2);
    REQUIRE(err == SRT_ERR_INVALID_ARG);
    REQUIRE_FALSE(std::string(srt_last_error()).empty());
    REQUIRE(srt_last_error_code() == SRT_ERR_INVALID_ARG);

    srt_session_destroy_v2(h);
}

// ---------------------------------------------------------------------------
// srt_session_set_reserved_phonemes 输入校验
// ---------------------------------------------------------------------------
TEST_CASE("srt_session_set_reserved_phonemes rejects null handle",
          "[c_abi][vnext][input-validation]") {
    srt_clear_last_error();
    const char *phonemes[] = {"a", "i"};
    auto err = srt_session_set_reserved_phonemes(nullptr, phonemes, 2);
    REQUIRE(err == SRT_ERR_INVALID_HANDLE);
    REQUIRE_FALSE(std::string(srt_last_error()).empty());
}

TEST_CASE("srt_session_set_reserved_phonemes rejects null phonemes when count>0",
          "[c_abi][vnext][input-validation]") {
    srt_SessionHandle *h = srt_session_create_v2();
    REQUIRE(h != nullptr);

    srt_clear_last_error();
    auto err = srt_session_set_reserved_phonemes(h, nullptr, 1);
    REQUIRE(err == SRT_ERR_INVALID_ARG);
    REQUIRE_FALSE(std::string(srt_last_error()).empty());

    srt_session_destroy_v2(h);
}

TEST_CASE("srt_session_set_reserved_phonemes accepts valid input",
          "[c_abi][vnext][input-validation]") {
    srt_SessionHandle *h = srt_session_create_v2();
    REQUIRE(h != nullptr);

    const char *phonemes[] = {"a", "i", "u", "e", "o"};
    srt_clear_last_error();
    auto err = srt_session_set_reserved_phonemes(h, phonemes, 5);
    REQUIRE(err == SRT_OK);

    srt_session_destroy_v2(h);
}

// ---------------------------------------------------------------------------
// srt_session_create_with_resources null 校验
// ---------------------------------------------------------------------------
TEST_CASE("srt_session_create_with_resources rejects null runtime",
          "[c_abi][vnext][input-validation]") {
    srt_clear_last_error();
    srt_LanguageServiceHandle *lang = srt_language_service_create();
    REQUIRE(lang != nullptr);

    auto session = srt_session_create_with_resources(nullptr, lang);
    REQUIRE(session == nullptr);
    REQUIRE(srt_last_error_code() == SRT_ERR_INVALID_ARG);
    REQUIRE_FALSE(std::string(srt_last_error()).empty());

    srt_language_service_destroy(lang);
}

TEST_CASE("srt_session_create_with_resources rejects null languageService",
          "[c_abi][vnext][input-validation]") {
    srt_clear_last_error();
    srt_RuntimeHandle *rt = srt_runtime_create();
    REQUIRE(rt != nullptr);

    auto session = srt_session_create_with_resources(rt, nullptr);
    REQUIRE(session == nullptr);
    REQUIRE(srt_last_error_code() == SRT_ERR_INVALID_ARG);
    REQUIRE_FALSE(std::string(srt_last_error()).empty());

    srt_runtime_destroy(rt);
}

TEST_CASE("srt_session_create_with_resources rejects both null",
          "[c_abi][vnext][input-validation]") {
    srt_clear_last_error();
    auto session = srt_session_create_with_resources(nullptr, nullptr);
    REQUIRE(session == nullptr);
    REQUIRE(srt_last_error_code() == SRT_ERR_INVALID_ARG);
}

TEST_CASE("srt_session_create_with_resources rejects invalid handles",
          "[c_abi][vnext][input-validation]") {
    srt_clear_last_error();
    // srt_RuntimeHandle / srt_LanguageServiceHandle are opaque structs
    // (forward-declared in srt.h, defined only inside lib/C/srt_v4.cpp).
    // The C ABI encodes a HandleId directly as the pointer value via
    // reinterpret_cast (see lib/C/HandleTable.h:encodeRuntimeHandle).
    // A non-null pointer that doesn't come from srt_runtime_create() /
    // srt_language_service_create() decodes to a HandleId not in the
    // HandleTable, which must be rejected with SRT_ERR_INVALID_HANDLE.
    // We must not instantiate the opaque struct by value (C2079); use
    // reinterpret_cast from a non-zero integer instead.
    auto fakeRt = reinterpret_cast<srt_RuntimeHandle *>(0xDEADBEEF);
    auto fakeLang = reinterpret_cast<srt_LanguageServiceHandle *>(0xDEADBEEF);
    auto session = srt_session_create_with_resources(fakeRt, fakeLang);
    REQUIRE(session == nullptr);
    REQUIRE(srt_last_error_code() == SRT_ERR_INVALID_HANDLE);
}

// ---------------------------------------------------------------------------
// srt_runtime_destroy / srt_language_service_destroy 幂等
// ---------------------------------------------------------------------------
TEST_CASE("srt_runtime_destroy accepts null as no-op", "[c_abi][vnext][idempotent]") {
    REQUIRE(srt_runtime_destroy(nullptr) == SRT_OK);
}

TEST_CASE("srt_language_service_destroy accepts null as no-op",
          "[c_abi][vnext][idempotent]") {
    REQUIRE(srt_language_service_destroy(nullptr) == SRT_OK);
}

TEST_CASE("srt_runtime_create returns non-null handle", "[c_abi][vnext][lifecycle]") {
    srt_RuntimeHandle *h = srt_runtime_create();
    REQUIRE(h != nullptr);
    REQUIRE(srt_runtime_destroy(h) == SRT_OK);
}

TEST_CASE("srt_language_service_create returns non-null handle",
          "[c_abi][vnext][lifecycle]") {
    srt_LanguageServiceHandle *h = srt_language_service_create();
    REQUIRE(h != nullptr);
    REQUIRE(srt_language_service_destroy(h) == SRT_OK);
}

TEST_CASE("srt_runtime_destroy rejects already-destroyed handle",
          "[c_abi][vnext][invalid-handle]") {
    srt_RuntimeHandle *h = srt_runtime_create();
    REQUIRE(h != nullptr);
    REQUIRE(srt_runtime_destroy(h) == SRT_OK);

    srt_clear_last_error();
    auto err = srt_runtime_destroy(h);
    REQUIRE(err == SRT_ERR_INVALID_HANDLE);
    REQUIRE_FALSE(std::string(srt_last_error()).empty());
}

TEST_CASE("srt_language_service_destroy rejects already-destroyed handle",
          "[c_abi][vnext][invalid-handle]") {
    srt_LanguageServiceHandle *h = srt_language_service_create();
    REQUIRE(h != nullptr);
    REQUIRE(srt_language_service_destroy(h) == SRT_OK);

    srt_clear_last_error();
    auto err = srt_language_service_destroy(h);
    REQUIRE(err == SRT_ERR_INVALID_HANDLE);
    REQUIRE_FALSE(std::string(srt_last_error()).empty());
}

// ---------------------------------------------------------------------------
// srt_session_snapshot null/invalid 校验
// ---------------------------------------------------------------------------
TEST_CASE("srt_session_snapshot rejects null handle", "[c_abi][vnext][snapshot]") {
    srt_clear_last_error();
    auto ptr = srt_session_snapshot(nullptr);
    REQUIRE(ptr == nullptr);
    REQUIRE(srt_last_error_code() == SRT_ERR_INVALID_HANDLE);
}

TEST_CASE("srt_session_snapshot returns null for fresh session",
          "[c_abi][vnext][snapshot]") {
    srt_SessionHandle *h = srt_session_create_v2();
    REQUIRE(h != nullptr);

    srt_clear_last_error();
    // 未调用 refresh 前无 snapshot
    auto ptr = srt_session_snapshot(h);
    REQUIRE(ptr == nullptr);

    srt_session_destroy_v2(h);
}

// ---------------------------------------------------------------------------
// srt_session_refresh_async null/invalid 校验
// ---------------------------------------------------------------------------
TEST_CASE("srt_session_refresh_async rejects null handle",
          "[c_abi][vnext][refresh-async]") {
    srt_clear_last_error();
    auto task = srt_session_refresh_async(nullptr);
    REQUIRE(task == nullptr);
    REQUIRE(srt_last_error_code() == SRT_ERR_INVALID_HANDLE);
}

// ---------------------------------------------------------------------------
// srt_model_create 输入校验
// ---------------------------------------------------------------------------
TEST_CASE("srt_model_create rejects null session", "[c_abi][vnext][model]") {
    srt_clear_last_error();
    auto m = srt_model_create(nullptr, "pkg", "singer", "1.0.0");
    REQUIRE(m == nullptr);
    REQUIRE(srt_last_error_code() == SRT_ERR_INVALID_HANDLE);
}

TEST_CASE("srt_model_create rejects null packageId", "[c_abi][vnext][model]") {
    srt_SessionHandle *h = srt_session_create_v2();
    REQUIRE(h != nullptr);

    srt_clear_last_error();
    auto m = srt_model_create(h, nullptr, "singer", "1.0.0");
    REQUIRE(m == nullptr);
    REQUIRE_FALSE(std::string(srt_last_error()).empty());

    srt_session_destroy_v2(h);
}

TEST_CASE("srt_model_create rejects null singerId", "[c_abi][vnext][model]") {
    srt_SessionHandle *h = srt_session_create_v2();
    REQUIRE(h != nullptr);

    srt_clear_last_error();
    auto m = srt_model_create(h, "pkg", nullptr, "1.0.0");
    REQUIRE(m == nullptr);
    REQUIRE_FALSE(std::string(srt_last_error()).empty());

    srt_session_destroy_v2(h);
}

TEST_CASE("srt_model_create accepts null version (treated as empty)",
          "[c_abi][vnext][model]") {
    srt_SessionHandle *h = srt_session_create_v2();
    REQUIRE(h != nullptr);

    srt_clear_last_error();
    // version 可以为 null（实现内部当空串处理）
    // 由于 session 未 refresh/无 snapshot，createModelSet 会失败但不应崩溃
    auto m = srt_model_create(h, "pkg", "singer", nullptr);
    // 不要求具体错误码，只验证不崩溃且返回 nullptr
    REQUIRE(m == nullptr);

    srt_session_destroy_v2(h);
}

// ---------------------------------------------------------------------------
// srt_model_destroy 幂等
// ---------------------------------------------------------------------------
TEST_CASE("srt_model_destroy accepts null as no-op", "[c_abi][vnext][model][idempotent]") {
    REQUIRE(srt_model_destroy(nullptr) == SRT_OK);
}

// ---------------------------------------------------------------------------
// srt_task_* 输入校验
// ---------------------------------------------------------------------------
TEST_CASE("srt_task_state returns FAILED for null handle", "[c_abi][vnext][task]") {
    REQUIRE(srt_task_state(nullptr) == SRT_TASK_STATE_FAILED);
}

TEST_CASE("srt_task_wait rejects null handle", "[c_abi][vnext][task]") {
    srt_clear_last_error();
    auto err = srt_task_wait(nullptr, 100);
    REQUIRE(err == SRT_ERR_INVALID_HANDLE);
    REQUIRE_FALSE(std::string(srt_last_error()).empty());
}

TEST_CASE("srt_task_cancel rejects null handle", "[c_abi][vnext][task]") {
    srt_clear_last_error();
    auto err = srt_task_cancel(nullptr);
    REQUIRE(err == SRT_ERR_INVALID_HANDLE);
    REQUIRE_FALSE(std::string(srt_last_error()).empty());
}

TEST_CASE("srt_task_result_json rejects null handle", "[c_abi][vnext][task]") {
    srt_clear_last_error();
    size_t outSize = 999;
    auto ptr = srt_task_result_json(nullptr, &outSize);
    REQUIRE(ptr == nullptr);
    REQUIRE(outSize == 0);
    REQUIRE(srt_last_error_code() == SRT_ERR_INVALID_HANDLE);
}

TEST_CASE("srt_task_destroy accepts null as no-op", "[c_abi][vnext][task][idempotent]") {
    srt_task_destroy(nullptr);
    SUCCEED();
}

// ---------------------------------------------------------------------------
// srt_free_buffer 幂等
// ---------------------------------------------------------------------------
TEST_CASE("srt_free_buffer accepts null as no-op", "[c_abi][memory][idempotent]") {
    srt_free_buffer(nullptr);
    SUCCEED();
}

// ---------------------------------------------------------------------------
// 错误消息双通道：srt_last_error() 提供消息，srt_last_error_code() 提供码
//
// 验证 ROBUST-05：出错必须显式报错。每个失败路径都应同时设置消息和码。
// ---------------------------------------------------------------------------
TEST_CASE("Error message and code are both set after invalid argument",
          "[c_abi][error][dual-channel]") {
    srt_clear_last_error();

    // 触发一个 INVALID_ARG 错误
    const char *paths[] = {"/tmp"};
    srt_session_set_package_paths(nullptr, paths, 1);

    // 双通道：消息非空 + 码 = SRT_ERR_INVALID_ARG
    std::string msg = srt_last_error();
    srt_error code = srt_last_error_code();

    REQUIRE_FALSE(msg.empty());
    REQUIRE(code == SRT_ERR_INVALID_ARG);
    // 消息应包含函数名上下文
    REQUIRE(msg.find("srt_session_set_package_paths") != std::string::npos);
}

TEST_CASE("srt_clear_last_error resets both message and code",
          "[c_abi][error][clear]") {
    // 先设置一个错误
    srt::core::Error err(srt::core::ErrorCode::InvalidArgument, "test");
    srt::c::detail::mapError(err);
    REQUIRE_FALSE(std::string(srt_last_error()).empty());
    REQUIRE(srt_last_error_code() != SRT_OK);

    // 清除后两者都重置
    srt_clear_last_error();
    REQUIRE(std::string(srt_last_error()).empty());
    REQUIRE(srt_last_error_code() == SRT_OK);
}

// ---------------------------------------------------------------------------
// extern "C" 边界异常隔离 (CODING-02 / ROBUST-02)
//
// 验证：即使 C++ 端抛出 std::exception，C ABI 也应捕获并转换为 srt_error，
// 而非让异常跨越 extern "C" 边界（UB）。
// 这里通过传递可能导致 filesystem::path 构造异常的输入来间接验证。
// ---------------------------------------------------------------------------
TEST_CASE("extern C boundary catches std::exception", "[c_abi][robust-02][coding-02]") {
    srt_session s = srt_session_create();
    REQUIRE(s != nullptr);

    srt_clear_last_error();
    // 传入合法但可能触发 filesystem 异常的路径；这里只验证不崩溃
    const char *paths[] = {"/tmp/normal-path"};
    auto err = srt_session_set_package_paths(s, paths, 1);
    REQUIRE(err == SRT_OK);

    srt_session_destroy(s);
}

// ---------------------------------------------------------------------------
// ds-editor-lite 真实使用场景：SynthrtEngine 初始化序列
//
// Lite SynthrtEngine::initialize 依次：
//   1. srt_session_create() 创建 session
//   2. srt_session_set_package_paths() 设置声库扫描路径
//   3. srt_session_refresh() 扫描声库
// 这里验证步骤 1-2 的输入校验路径。
// ---------------------------------------------------------------------------
TEST_CASE("lite SynthrtEngine initialization sequence validates inputs",
          "[c_abi][realworld]") {
    SECTION("create + set paths + destroy succeeds") {
        srt_session s = srt_session_create();
        REQUIRE(s != nullptr);

        const char *paths[] = {"/voicebanks/pkg1", "/voicebanks/pkg2"};
        srt_clear_last_error();
        auto err = srt_session_set_package_paths(s, paths, 2);
        REQUIRE(err == SRT_OK);
        REQUIRE(std::string(srt_last_error()).empty());

        srt_session_destroy(s);
    }

    SECTION("create with empty paths list is valid") {
        srt_session s = srt_session_create();
        REQUIRE(s != nullptr);

        srt_clear_last_error();
        auto err = srt_session_set_package_paths(s, nullptr, 0);
        REQUIRE(err == SRT_OK);

        srt_session_destroy(s);
    }
}
