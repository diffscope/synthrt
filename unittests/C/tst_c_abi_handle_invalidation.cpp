// C ABI 句柄失效测试 — 测试矩阵 C-041 ~ C-060.
//
// 覆盖 srt.h 公开 API 在句柄被 destroy 后的行为：非 destroy 接口在被销毁的
// 句柄上调用时应返回 SRT_ERR_INVALID_HANDLE 并设置 last_error，且程序不崩溃。
//
// 互补关系（不重复已有测试）：
//   * C-011 (task_state on destroyed handle, 先 wait 到终态) — C-052 测试 destroy
//     后立刻读 state（不 wait），验证非终态下 destroy 同样让 state 读返回 FAILED。
//   * C-014 (session_destroy_v2 二次调用) — C-056 扩展到三次调用稳定性。
//   * C-022 (runtime_destroy 二次调用) — C-054 扩展到三次调用稳定性 + last_error
//     验证。
//   * C-024 (language_service_destroy 二次调用) — C-055 扩展到三次调用稳定性。
//   * C-035 (task_state on destroyed with error message) — C-052 验证 destroy 后
//     立刻（不 wait）读 state，互补覆盖非终态路径。
//   * C-037 (HandleTable destroy idempotent, runtime 三次) — C-054/C-055/C-056
//     补充 language_service / session 的三次稳定性 + last_error_code/message 验证。
//
// API 实现见 lib/C/srt_v4.cpp；句柄表见 lib/C/HandleTable.h。
//
// 注意：句柄编码规则（HandleTable.h）将 HandleId 直接 reinterpret_cast 为指针，
// 因此 destroy 后同一指针值仍可被 decode 为旧 id，但 lookup 返回空 → INVALID_HANDLE。
// 这保证了 destroy 后的句柄"稳定失效"，而非 use-after-free。

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include <synthrt/C/srt.h>

// C-048 需要直接向 modelTable 插入一个 ModelData 来获得合法的 ModelHandle：
// srt_model_create 要求真实 singer + 已刷新的 snapshot，测试环境不便构造真实
// voicebank 数据。此处复用 tests/abi/tst_session_handle.cpp 的内部表注入手法。
// lib/C 已被 CMakeLists.txt 加入本目标的 PRIVATE include 路径。
#include "HandleTable.h"

// ---------------------------------------------------------------------------
// C-041: srt_session_set_roots on a destroyed handle returns INVALID_HANDLE
// ---------------------------------------------------------------------------
TEST_CASE("C-041: srt_session_set_roots on destroyed handle returns INVALID_HANDLE",
          "[c][abi][handle-invalidation]") {
    srt_SessionHandle *h = srt_session_create_v2();
    REQUIRE(h != nullptr);
    REQUIRE(srt_session_destroy_v2(h) == SRT_OK);

    const char *roots[] = {"."};
    srt_clear_last_error();
    auto err = srt_session_set_roots(h, roots, 1);
    REQUIRE(err == SRT_ERR_INVALID_HANDLE);
    REQUIRE(srt_last_error_code() == SRT_ERR_INVALID_HANDLE);
    std::string msg = srt_last_error();
    REQUIRE_FALSE(msg.empty());
    REQUIRE(msg.find("invalid handle") != std::string::npos);

    // 句柄已销毁，无需再次 destroy
}

// ---------------------------------------------------------------------------
// C-042: srt_session_set_reserved_phonemes on a destroyed handle returns
// INVALID_HANDLE
// ---------------------------------------------------------------------------
TEST_CASE("C-042: srt_session_set_reserved_phonemes on destroyed handle returns INVALID_HANDLE",
          "[c][abi][handle-invalidation]") {
    srt_SessionHandle *h = srt_session_create_v2();
    REQUIRE(h != nullptr);
    REQUIRE(srt_session_destroy_v2(h) == SRT_OK);

    const char *phons[] = {"a", "i", "u"};
    srt_clear_last_error();
    auto err = srt_session_set_reserved_phonemes(h, phons, 3);
    REQUIRE(err == SRT_ERR_INVALID_HANDLE);
    REQUIRE(srt_last_error_code() == SRT_ERR_INVALID_HANDLE);
    std::string msg = srt_last_error();
    REQUIRE_FALSE(msg.empty());
    REQUIRE(msg.find("invalid handle") != std::string::npos);
}

// ---------------------------------------------------------------------------
// C-043: srt_session_refresh_async on a destroyed handle returns NULL
// ---------------------------------------------------------------------------
TEST_CASE("C-043: srt_session_refresh_async on destroyed handle returns NULL",
          "[c][abi][handle-invalidation]") {
    srt_SessionHandle *h = srt_session_create_v2();
    REQUIRE(h != nullptr);
    REQUIRE(srt_session_destroy_v2(h) == SRT_OK);

    srt_clear_last_error();
    srt_TaskHandle *task = srt_session_refresh_async(h);
    REQUIRE(task == nullptr);
    REQUIRE(srt_last_error_code() == SRT_ERR_INVALID_HANDLE);
    std::string msg = srt_last_error();
    REQUIRE_FALSE(msg.empty());
    REQUIRE(msg.find("invalid handle") != std::string::npos);
}

// ---------------------------------------------------------------------------
// C-044: srt_session_snapshot on a destroyed handle returns NULL
// ---------------------------------------------------------------------------
TEST_CASE("C-044: srt_session_snapshot on destroyed handle returns NULL",
          "[c][abi][handle-invalidation]") {
    srt_SessionHandle *h = srt_session_create_v2();
    REQUIRE(h != nullptr);
    REQUIRE(srt_session_destroy_v2(h) == SRT_OK);

    srt_clear_last_error();
    const void *snap = srt_session_snapshot(h);
    REQUIRE(snap == nullptr);
    REQUIRE(srt_last_error_code() == SRT_ERR_INVALID_HANDLE);
    std::string msg = srt_last_error();
    REQUIRE_FALSE(msg.empty());
    REQUIRE(msg.find("invalid handle") != std::string::npos);
}

// ---------------------------------------------------------------------------
// C-045: srt_session_create_with_resources on a destroyed runtime handle
// returns NULL with INVALID_HANDLE
// ---------------------------------------------------------------------------
TEST_CASE("C-045: srt_session_create_with_resources on destroyed runtime handle returns NULL",
          "[c][abi][handle-invalidation]") {
    srt_RuntimeHandle *rt = srt_runtime_create();
    REQUIRE(rt != nullptr);
    srt_LanguageServiceHandle *lang = srt_language_service_create();
    REQUIRE(lang != nullptr);

    REQUIRE(srt_runtime_destroy(rt) == SRT_OK);

    srt_clear_last_error();
    srt_SessionHandle *session = srt_session_create_with_resources(rt, lang);
    REQUIRE(session == nullptr);
    REQUIRE(srt_last_error_code() == SRT_ERR_INVALID_HANDLE);
    std::string msg = srt_last_error();
    REQUIRE_FALSE(msg.empty());
    REQUIRE(msg.find("invalid runtime handle") != std::string::npos);

    // lang 仍有效，需要释放
    REQUIRE(srt_language_service_destroy(lang) == SRT_OK);
}

// ---------------------------------------------------------------------------
// C-046: srt_session_create_with_resources on a destroyed languageService
// handle returns NULL with INVALID_HANDLE
// ---------------------------------------------------------------------------
TEST_CASE("C-046: srt_session_create_with_resources on destroyed languageService handle returns NULL",
          "[c][abi][handle-invalidation]") {
    srt_RuntimeHandle *rt = srt_runtime_create();
    REQUIRE(rt != nullptr);
    srt_LanguageServiceHandle *lang = srt_language_service_create();
    REQUIRE(lang != nullptr);

    REQUIRE(srt_language_service_destroy(lang) == SRT_OK);

    srt_clear_last_error();
    srt_SessionHandle *session = srt_session_create_with_resources(rt, lang);
    REQUIRE(session == nullptr);
    REQUIRE(srt_last_error_code() == SRT_ERR_INVALID_HANDLE);
    std::string msg = srt_last_error();
    REQUIRE_FALSE(msg.empty());
    REQUIRE(msg.find("invalid language service handle") != std::string::npos);

    // rt 仍有效，需要释放
    REQUIRE(srt_runtime_destroy(rt) == SRT_OK);
}

// ---------------------------------------------------------------------------
// C-047: srt_model_create on a destroyed session handle returns NULL
// ---------------------------------------------------------------------------
TEST_CASE("C-047: srt_model_create on destroyed session handle returns NULL",
          "[c][abi][handle-invalidation]") {
    srt_SessionHandle *h = srt_session_create_v2();
    REQUIRE(h != nullptr);
    REQUIRE(srt_session_destroy_v2(h) == SRT_OK);

    srt_clear_last_error();
    srt_ModelHandle *m = srt_model_create(h, "pkg", "singer", "1.0.0");
    REQUIRE(m == nullptr);
    REQUIRE(srt_last_error_code() == SRT_ERR_INVALID_HANDLE);
    std::string msg = srt_last_error();
    REQUIRE_FALSE(msg.empty());
    REQUIRE(msg.find("invalid session handle") != std::string::npos);
}

// ---------------------------------------------------------------------------
// C-048: srt_model_destroy double call on same handle (already destroyed)
// returns INVALID_HANDLE
//
// srt_model_create 需要真实 singer + snapshot，测试环境不便构造。这里直接向
// modelTable 注入一个 ModelData（与 tests/abi/tst_session_handle.cpp 同手法），
// 获得合法 ModelHandle 后验证 destroy 的幂等失效语义。
// ---------------------------------------------------------------------------
TEST_CASE("C-048: srt_model_destroy double call returns INVALID_HANDLE",
          "[c][abi][handle-invalidation]") {
    auto data = std::make_shared<srt::c_api::ModelData>();
    data->m_packageId = "pkg";
    data->m_singerId = "singer";
    data->m_version = "1.0";
    srt::c_api::HandleId id = srt::c_api::modelTable().create(data);
    REQUIRE(id != srt::c_api::kInvalidHandle);

    srt_ModelHandle *m = srt::c_api::encodeModelHandle(id);
    REQUIRE(m != nullptr);

    // 第一次 destroy 应成功
    srt_clear_last_error();
    REQUIRE(srt_model_destroy(m) == SRT_OK);
    REQUIRE(srt_last_error_code() == SRT_OK);

    // 第二次 destroy 同一 handle 应返回 INVALID_HANDLE
    srt_clear_last_error();
    REQUIRE(srt_model_destroy(m) == SRT_ERR_INVALID_HANDLE);
    REQUIRE(srt_last_error_code() == SRT_ERR_INVALID_HANDLE);
    std::string msg = srt_last_error();
    REQUIRE_FALSE(msg.empty());
    REQUIRE(msg.find("invalid handle") != std::string::npos);
}

// ---------------------------------------------------------------------------
// C-049: srt_task_wait on a destroyed task handle returns INVALID_HANDLE
// ---------------------------------------------------------------------------
TEST_CASE("C-049: srt_task_wait on destroyed task handle returns INVALID_HANDLE",
          "[c][abi][handle-invalidation]") {
    srt_SessionHandle *h = srt_session_create_v2();
    REQUIRE(h != nullptr);

    srt_TaskHandle *task = srt_session_refresh_async(h);
    REQUIRE(task != nullptr);

    // 先驱动到终态再 destroy，避免后台 watcher 持有 shared_ptr 期间的时序干扰。
    REQUIRE(srt_task_wait(task, 30000) == SRT_OK);
    srt_task_destroy(task);

    srt_clear_last_error();
    auto err = srt_task_wait(task, 100);
    REQUIRE(err == SRT_ERR_INVALID_HANDLE);
    REQUIRE(srt_last_error_code() == SRT_ERR_INVALID_HANDLE);
    std::string msg = srt_last_error();
    REQUIRE_FALSE(msg.empty());
    REQUIRE(msg.find("invalid handle") != std::string::npos);

    srt_session_destroy_v2(h);
}

// ---------------------------------------------------------------------------
// C-050: srt_task_cancel on a destroyed task handle returns INVALID_HANDLE
// ---------------------------------------------------------------------------
TEST_CASE("C-050: srt_task_cancel on destroyed task handle returns INVALID_HANDLE",
          "[c][abi][handle-invalidation]") {
    srt_SessionHandle *h = srt_session_create_v2();
    REQUIRE(h != nullptr);

    srt_TaskHandle *task = srt_session_refresh_async(h);
    REQUIRE(task != nullptr);

    REQUIRE(srt_task_wait(task, 30000) == SRT_OK);
    srt_task_destroy(task);

    srt_clear_last_error();
    auto err = srt_task_cancel(task);
    REQUIRE(err == SRT_ERR_INVALID_HANDLE);
    REQUIRE(srt_last_error_code() == SRT_ERR_INVALID_HANDLE);
    std::string msg = srt_last_error();
    REQUIRE_FALSE(msg.empty());
    REQUIRE(msg.find("invalid handle") != std::string::npos);

    srt_session_destroy_v2(h);
}

// ---------------------------------------------------------------------------
// C-051: srt_task_result_json on a destroyed task handle returns NULL
// ---------------------------------------------------------------------------
TEST_CASE("C-051: srt_task_result_json on destroyed task handle returns NULL",
          "[c][abi][handle-invalidation]") {
    srt_SessionHandle *h = srt_session_create_v2();
    REQUIRE(h != nullptr);

    srt_TaskHandle *task = srt_session_refresh_async(h);
    REQUIRE(task != nullptr);

    REQUIRE(srt_task_wait(task, 30000) == SRT_OK);
    srt_task_destroy(task);

    srt_clear_last_error();
    size_t outSize = 999;
    const char *json = srt_task_result_json(task, &outSize);
    REQUIRE(json == nullptr);
    REQUIRE(outSize == 0);
    REQUIRE(srt_last_error_code() == SRT_ERR_INVALID_HANDLE);
    std::string msg = srt_last_error();
    REQUIRE_FALSE(msg.empty());
    REQUIRE(msg.find("invalid handle") != std::string::npos);

    srt_session_destroy_v2(h);
}

// ---------------------------------------------------------------------------
// C-052: srt_task_state on a destroyed task handle returns FAILED
// (与 C-011/C-035 互补：验证 destroy 后立刻读 state，不先 wait 到终态)
//
// C-011/C-035 先 wait 到终态再 destroy 再读 state；本用例在 task 仍可能处于
// RUNNING 时直接 destroy 并立刻读 state，验证非终态路径下 destroy 同样让
// state 读返回 FAILED（句柄表项已移除，与底层 TaskData 是否终态无关）。
// ---------------------------------------------------------------------------
TEST_CASE("C-052: srt_task_state on destroyed handle returns FAILED (no wait before destroy)",
          "[c][abi][handle-invalidation]") {
    srt_SessionHandle *h = srt_session_create_v2();
    REQUIRE(h != nullptr);

    // 设置 roots 指向真实目录，让 refresh 有真实文件系统工作，增大 RUNNING 窗口。
    const char *roots[] = {"."};
    REQUIRE(srt_session_set_roots(h, roots, 1) == SRT_OK);

    srt_TaskHandle *task = srt_session_refresh_async(h);
    REQUIRE(task != nullptr);

    // 不 wait，直接 destroy。task 表项立即移除；底层 TaskData 由 watcher 线程的
    // shared_ptr 保活，destroy 不会中断运行中的 refresh。
    srt_task_destroy(task);

    // 立刻读 state：句柄已失效，无论底层任务是否终态都返回 FAILED。
    srt_clear_last_error();
    srt_TaskState st = srt_task_state(task);
    REQUIRE(st == SRT_TASK_STATE_FAILED);
    REQUIRE(srt_last_error_code() == SRT_ERR_INVALID_HANDLE);
    std::string msg = srt_last_error();
    REQUIRE_FALSE(msg.empty());
    REQUIRE(msg.find("invalid handle") != std::string::npos);

    // session 也可安全 destroy：task 的 watcher 持有 SessionData shared_ptr，
    // session 表项移除后底层对象仍存活至 watcher 完成。
    srt_session_destroy_v2(h);
}

// ---------------------------------------------------------------------------
// C-053: srt_session_set_roots on a destroyed handle does not crash on NULL
// roots (组合：destroyed handle + NULL roots + count>0)
//
// 实现细节（lib/C/srt_v4.cpp srt_session_set_roots）：count>0 && roots==NULL 的
// 参数校验先于句柄表 lookup 执行，因此 destroyed handle + NULL roots 组合实际
// 返回 SRT_ERR_INVALID_ARG。本用例的核心验证点是"不崩溃"，错误码接受
// INVALID_ARG 或 INVALID_HANDLE 任一（实现可调整校验顺序）。
// ---------------------------------------------------------------------------
TEST_CASE("C-053: srt_session_set_roots on destroyed handle with NULL roots does not crash",
          "[c][abi][handle-invalidation]") {
    srt_SessionHandle *h = srt_session_create_v2();
    REQUIRE(h != nullptr);
    REQUIRE(srt_session_destroy_v2(h) == SRT_OK);

    srt_clear_last_error();
    // destroyed handle + NULL roots + count=1：当前实现先命中 NULL roots 校验，
    // 返回 INVALID_ARG。若实现调整为先 lookup 句柄，则返回 INVALID_HANDLE。
    // 两者都是合法的"拒绝错误"，核心要求是不崩溃且设置错误。
    auto err = srt_session_set_roots(h, nullptr, 1);
    REQUIRE((err == SRT_ERR_INVALID_ARG || err == SRT_ERR_INVALID_HANDLE));
    REQUIRE(srt_last_error_code() == err);
    REQUIRE_FALSE(std::string(srt_last_error()).empty());
}

// ---------------------------------------------------------------------------
// C-054: srt_runtime_destroy triple call stability
// (与 C-037 互补：补充 last_error_code / "invalid handle" 消息验证)
//
// C-037 已验证 runtime_destroy 三次调用稳定性，但仅在第二次调用验证 last_error
// 非空。本用例进一步验证第二、三次调用均设置 last_error_code == INVALID_HANDLE
// 且消息包含 "invalid handle"，确保 destroy 失败路径的双通道错误报告完整。
// ---------------------------------------------------------------------------
TEST_CASE("C-054: srt_runtime_destroy triple call stability",
          "[c][abi][handle-invalidation]") {
    srt_RuntimeHandle *rt = srt_runtime_create();
    REQUIRE(rt != nullptr);

    // 第一次 destroy：成功
    srt_clear_last_error();
    REQUIRE(srt_runtime_destroy(rt) == SRT_OK);
    REQUIRE(srt_last_error_code() == SRT_OK);

    // 第二次 destroy：INVALID_HANDLE + 完整错误信息
    srt_clear_last_error();
    REQUIRE(srt_runtime_destroy(rt) == SRT_ERR_INVALID_HANDLE);
    REQUIRE(srt_last_error_code() == SRT_ERR_INVALID_HANDLE);
    std::string msg2 = srt_last_error();
    REQUIRE_FALSE(msg2.empty());
    REQUIRE(msg2.find("invalid handle") != std::string::npos);

    // 第三次 destroy：仍稳定返回 INVALID_HANDLE（句柄表项移除是永久的）
    srt_clear_last_error();
    REQUIRE(srt_runtime_destroy(rt) == SRT_ERR_INVALID_HANDLE);
    REQUIRE(srt_last_error_code() == SRT_ERR_INVALID_HANDLE);
    std::string msg3 = srt_last_error();
    REQUIRE_FALSE(msg3.empty());
    REQUIRE(msg3.find("invalid handle") != std::string::npos);
}

// ---------------------------------------------------------------------------
// C-055: srt_language_service_destroy triple call stability
// (C-024 仅验证二次调用；本用例扩展到三次 + last_error_code/message 验证)
// ---------------------------------------------------------------------------
TEST_CASE("C-055: srt_language_service_destroy triple call stability",
          "[c][abi][handle-invalidation]") {
    srt_LanguageServiceHandle *lang = srt_language_service_create();
    REQUIRE(lang != nullptr);

    // 第一次 destroy：成功
    srt_clear_last_error();
    REQUIRE(srt_language_service_destroy(lang) == SRT_OK);
    REQUIRE(srt_last_error_code() == SRT_OK);

    // 第二次 destroy：INVALID_HANDLE + 完整错误信息
    srt_clear_last_error();
    REQUIRE(srt_language_service_destroy(lang) == SRT_ERR_INVALID_HANDLE);
    REQUIRE(srt_last_error_code() == SRT_ERR_INVALID_HANDLE);
    std::string msg2 = srt_last_error();
    REQUIRE_FALSE(msg2.empty());
    REQUIRE(msg2.find("invalid handle") != std::string::npos);

    // 第三次 destroy：仍稳定返回 INVALID_HANDLE
    srt_clear_last_error();
    REQUIRE(srt_language_service_destroy(lang) == SRT_ERR_INVALID_HANDLE);
    REQUIRE(srt_last_error_code() == SRT_ERR_INVALID_HANDLE);
    std::string msg3 = srt_last_error();
    REQUIRE_FALSE(msg3.empty());
    REQUIRE(msg3.find("invalid handle") != std::string::npos);
}

// ---------------------------------------------------------------------------
// C-056: srt_session_destroy_v2 triple call stability
// (C-014 仅验证二次调用；本用例扩展到三次 + last_error_code/message 验证)
// ---------------------------------------------------------------------------
TEST_CASE("C-056: srt_session_destroy_v2 triple call stability",
          "[c][abi][handle-invalidation]") {
    srt_SessionHandle *h = srt_session_create_v2();
    REQUIRE(h != nullptr);

    // 第一次 destroy：成功
    srt_clear_last_error();
    REQUIRE(srt_session_destroy_v2(h) == SRT_OK);
    REQUIRE(srt_last_error_code() == SRT_OK);

    // 第二次 destroy：INVALID_HANDLE + 完整错误信息
    srt_clear_last_error();
    REQUIRE(srt_session_destroy_v2(h) == SRT_ERR_INVALID_HANDLE);
    REQUIRE(srt_last_error_code() == SRT_ERR_INVALID_HANDLE);
    std::string msg2 = srt_last_error();
    REQUIRE_FALSE(msg2.empty());
    REQUIRE(msg2.find("invalid handle") != std::string::npos);

    // 第三次 destroy：仍稳定返回 INVALID_HANDLE
    srt_clear_last_error();
    REQUIRE(srt_session_destroy_v2(h) == SRT_ERR_INVALID_HANDLE);
    REQUIRE(srt_last_error_code() == SRT_ERR_INVALID_HANDLE);
    std::string msg3 = srt_last_error();
    REQUIRE_FALSE(msg3.empty());
    REQUIRE(msg3.find("invalid handle") != std::string::npos);
}

// ---------------------------------------------------------------------------
// C-057: srt_model_create on a session created from a destroyed runtime handle
// (链式失败：destroyed runtime → create_with_resources 返回 NULL → model_create
// 收到 NULL session 返回 NULL)
//
// 验证当 runtime 句柄失效时，依赖链下游的 session/model 创建也正确失败，
// 不会因悬空指针崩溃。本用例互补 C-045（仅验证 create_with_resources 失败）
// 与 C-009（仅验证 model_create(NULL) 单点失败），覆盖完整的失败传播链。
// ---------------------------------------------------------------------------
TEST_CASE("C-057: srt_model_create on session from destroyed runtime handle returns NULL",
          "[c][abi][handle-invalidation]") {
    srt_RuntimeHandle *rt = srt_runtime_create();
    REQUIRE(rt != nullptr);
    srt_LanguageServiceHandle *lang = srt_language_service_create();
    REQUIRE(lang != nullptr);

    // 销毁 runtime，违反借用契约的前置条件 → create_with_resources 必须失败
    REQUIRE(srt_runtime_destroy(rt) == SRT_OK);

    // 链式第一步：session 创建因 runtime 失效而返回 NULL
    srt_clear_last_error();
    srt_SessionHandle *session = srt_session_create_with_resources(rt, lang);
    REQUIRE(session == nullptr);
    REQUIRE(srt_last_error_code() == SRT_ERR_INVALID_HANDLE);

    // 链式第二步：将 NULL session 传给 model_create，同样返回 NULL
    // （srt_model_create 的 NULL session 守卫返回 INVALID_HANDLE）
    srt_clear_last_error();
    srt_ModelHandle *m = srt_model_create(session, "pkg", "singer", "1.0.0");
    REQUIRE(m == nullptr);
    REQUIRE(srt_last_error_code() == SRT_ERR_INVALID_HANDLE);
    REQUIRE_FALSE(std::string(srt_last_error()).empty());

    // lang 仍有效，需要释放
    REQUIRE(srt_language_service_destroy(lang) == SRT_OK);
}

// ---------------------------------------------------------------------------
// C-058: srt_task_destroy is idempotent (double call on same handle is safe)
//
// srt_task_destroy 返回 void，文档未承诺二次调用的返回值，但要求"不崩溃"。
// 实现上第二次 destroy 会调用 HandleTable::destroy，返回 false（表项已移除）
// 但被忽略，因此是安全的 no-op。本用例验证双重 destroy 不引发崩溃。
// ---------------------------------------------------------------------------
TEST_CASE("C-058: srt_task_destroy is idempotent (double call is safe)",
          "[c][abi][handle-invalidation]") {
    srt_SessionHandle *h = srt_session_create_v2();
    REQUIRE(h != nullptr);

    srt_TaskHandle *task = srt_session_refresh_async(h);
    REQUIRE(task != nullptr);

    // 先 wait 到终态，避免 watcher 线程时序干扰
    REQUIRE(srt_task_wait(task, 30000) == SRT_OK);

    // 第一次 destroy：正常释放表项
    REQUIRE_NOTHROW(srt_task_destroy(task));

    // 第二次 destroy：表项已移除，HandleTable::destroy 返回 false 但被忽略，
    // 接口仍安全返回（void）。验证不崩溃。
    REQUIRE_NOTHROW(srt_task_destroy(task));

    // 第三次 destroy：仍应安全
    REQUIRE_NOTHROW(srt_task_destroy(task));

    srt_session_destroy_v2(h);
}

// ---------------------------------------------------------------------------
// C-059: destroyed handle 不影响新创建的句柄（生命周期隔离）
//
// 验证句柄表的隔离性：销毁一个 session 不应影响后续创建的 session。每个
// session 拥有独立的表项与底层 SessionData，destroy 仅移除对应表项。
// ---------------------------------------------------------------------------
TEST_CASE("C-059: destroyed handle does not affect newly created handles",
          "[c][abi][handle-invalidation]") {
    // 创建 session A 并销毁
    srt_SessionHandle *a = srt_session_create_v2();
    REQUIRE(a != nullptr);
    REQUIRE(srt_session_destroy_v2(a) == SRT_OK);

    // 在 A 销毁后创建 session B，B 应完全可用
    srt_SessionHandle *b = srt_session_create_v2();
    REQUIRE(b != nullptr);
    REQUIRE(b != a); // 不同句柄值（句柄 id 递增）

    const char *roots[] = {"."};
    srt_clear_last_error();
    REQUIRE(srt_session_set_roots(b, roots, 1) == SRT_OK);
    REQUIRE(srt_last_error_code() == SRT_OK);

    const char *phons[] = {"a", "i"};
    REQUIRE(srt_session_set_reserved_phonemes(b, phons, 2) == SRT_OK);

    // B 的 snapshot 在未 refresh 前为 NULL（合法）
    REQUIRE(srt_session_snapshot(b) == nullptr);

    // B 的 refresh_async 应正常工作
    srt_TaskHandle *task = srt_session_refresh_async(b);
    REQUIRE(task != nullptr);
    REQUIRE(srt_task_wait(task, 30000) == SRT_OK);
    srt_task_destroy(task);

    // 同时验证 A 仍处于失效状态（不影响 B）
    srt_clear_last_error();
    REQUIRE(srt_session_set_roots(a, roots, 1) == SRT_ERR_INVALID_HANDLE);

    // 清理 B
    REQUIRE(srt_session_destroy_v2(b) == SRT_OK);
}

// ---------------------------------------------------------------------------
// C-060: 句柄表并发 destroy 安全（2 个线程同时 destroy 同一 handle，
// 恰好一个成功）
//
// 验证 HandleTable 的 mutex 保护的原子性：两个线程并发 destroy 同一 session
// 时，恰好一个返回 SRT_OK（移除表项），另一个返回 SRT_ERR_INVALID_HANDLE
// （表项已不存在）。不会双重释放或崩溃。
// ---------------------------------------------------------------------------
TEST_CASE("C-060: concurrent destroy of same handle is safe (exactly one succeeds)",
          "[c][abi][handle-invalidation]") {
    srt_SessionHandle *h = srt_session_create_v2();
    REQUIRE(h != nullptr);

    std::mutex startMtx;
    std::condition_variable startCv;
    bool ready = false;
    std::atomic<int> okCount{0};
    std::atomic<int> invalidCount{0};
    std::atomic<int> otherCount{0};

    auto worker = [&]() {
        // 屏障：等待主线程发出 ready 信号，确保两线程尽量同时调用 destroy
        std::unique_lock<std::mutex> lock(startMtx);
        startCv.wait(lock, [&] { return ready; });
        lock.unlock();

        srt_clear_last_error();
        srt_error err = srt_session_destroy_v2(h);
        if (err == SRT_OK) {
            okCount.fetch_add(1, std::memory_order_relaxed);
        } else if (err == SRT_ERR_INVALID_HANDLE) {
            invalidCount.fetch_add(1, std::memory_order_relaxed);
        } else {
            otherCount.fetch_add(1, std::memory_order_relaxed);
        }
    };

    std::thread t1(worker);
    std::thread t2(worker);

    // 给两个线程时间进入 wait，最大化并发概率
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    {
        std::lock_guard<std::mutex> lock(startMtx);
        ready = true;
    }
    startCv.notify_all();

    t1.join();
    t2.join();

    // 恰好一个线程成功（SRT_OK），另一个拿到 INVALID_HANDLE。
    // HandleTable::destroy 的 mutex 保证表项移除是原子的，不会双重释放。
    REQUIRE(okCount.load() == 1);
    REQUIRE(invalidCount.load() == 1);
    REQUIRE(otherCount.load() == 0);

    // 并发 destroy 后再调一次，应稳定返回 INVALID_HANDLE（表项已移除）
    srt_clear_last_error();
    REQUIRE(srt_session_destroy_v2(h) == SRT_ERR_INVALID_HANDLE);
}
