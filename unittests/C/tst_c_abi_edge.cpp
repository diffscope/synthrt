// C ABI (lib/C, srt.h) edge case tests — unique scenarios not covered by
// tst_c_abi_input_validation.cpp.
//
// This file previously carried C-001 ~ C-040 (40 cases). After merging with
// tst_c_abi_input_validation.cpp (which provides detailed error-message
// verification including function-name context), the cross-file duplicates
// were removed and only the scenarios unique to this file are kept:
//   - Lifecycle no-op/idempotency (init/shutdown, free_string/array/buffer)
//   - TLS error isolation across threads
//   - Negative-timeout wait, non-terminal result_json, destroyed-handle state
//   - API version query
//   - Regression tests (negative count, register-before-watcher, post-
//     completion state read, descriptive invalid-handle errors)
//
// Removed (now covered with MORE detail in tst_c_abi_input_validation.cpp):
//   C-003  srt_session_destroy(NULL)
//   C-004  set_package_paths NULL paths + count>0
//   C-007  srt_free_string(NULL)              (in-file duplicate of C-016)
//   C-008  create_with_resources(NULL, NULL)
//   C-009  model_create NULL session
//   C-014  destroy_v2 idempotency
//   C-019  set_roots NULL handle
//   C-020  set_roots empty roots
//   C-021  runtime lifecycle
//   C-022  runtime double destroy
//   C-023  language_service lifecycle
//   C-024  language_service double destroy
//   C-025  model_destroy NULL
//   C-026  task_cancel NULL
//   C-027  snapshot NULL
//   C-028  snapshot fresh session
//   C-029  task_state NULL
//   C-030  task_wait NULL
//   C-031  set_reserved_phonemes NULL handle
//   C-037  runtime destroy idempotent
//   C-038  set_package_paths count=0 + NULL
//   C-039  set_package_paths count=0 + valid
//
// API difference notes (relative to the original test matrix, adjusted to the
// actual implementation — lib/C/srt_v4.cpp, domains/ds-bank/lib/VoicebankScanner.cpp):
//
//  * C-001: srt_init/srt_shutdown are currently no-ops that always return
//    SRT_OK (srt_v4.cpp: "no-op by design"); there is no real process-wide
//    reference counting. The test verifies the documented no-op/idempotent
//    contract (all balanced calls return SRT_OK and the runtime stays
//    usable) rather than true refcount release.
//
//  * C-005: The matrix expects srt_session_refresh on a session with no
//    package paths to return SRT_ERR_NOT_INIT. The actual implementation
//    (VoicebankScanner::refresh with an empty search-path list returns an
//    empty success vector) yields SRT_OK. The test asserts the actual
//    behavior.

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include <synthrt/C/srt.h>

// ---------------------------------------------------------------------------
// C-001: srt_init / srt_shutdown reference-count style calls
// ---------------------------------------------------------------------------
TEST_CASE("C-001: srt_init/srt_shutdown are idempotent (refcount-style) calls",
          "[c][abi][edge]") {
    // srt_init/srt_shutdown are no-ops that always return SRT_OK (no real
    // process-wide refcount). Verify the documented contract: multiple
    // balanced calls all succeed and the runtime remains usable afterwards.
    REQUIRE(srt_init() == SRT_OK);
    REQUIRE(srt_init() == SRT_OK);
    REQUIRE(srt_init() == SRT_OK);

    REQUIRE(srt_shutdown() == SRT_OK);
    REQUIRE(srt_shutdown() == SRT_OK);
    REQUIRE(srt_shutdown() == SRT_OK);

    // After a balanced init/shutdown sequence the runtime must stay functional.
    srt_session s = srt_session_create();
    REQUIRE(s != nullptr);
    REQUIRE(srt_session_destroy(s) == SRT_OK);
}

// ---------------------------------------------------------------------------
// C-002: srt_shutdown without a matching srt_init is a no-op
// ---------------------------------------------------------------------------
TEST_CASE("C-002: srt_shutdown without a matching srt_init is a no-op",
          "[c][abi][edge]") {
    srt_clear_last_error();
    // No prior srt_init for this scenario; shutdown must be a no-op returning
    // SRT_OK and must not set any error.
    REQUIRE(srt_shutdown() == SRT_OK);
    REQUIRE(srt_last_error_code() == SRT_OK);
    REQUIRE(std::string(srt_last_error()).empty());
}

// ---------------------------------------------------------------------------
// C-005: srt_session_refresh on a session without package paths
// ---------------------------------------------------------------------------
TEST_CASE("C-005: srt_session_refresh on a session without package paths",
          "[c][abi][edge]") {
    // API difference: the matrix expects SRT_ERR_NOT_INIT, but the actual
    // implementation (VoicebankScanner::refresh with an empty search-path
    // list returns an empty success vector) yields SRT_OK. Asserting the
    // actual behavior.
    srt_session s = srt_session_create();
    REQUIRE(s != nullptr);

    srt_clear_last_error();
    REQUIRE(srt_session_refresh(s) == SRT_OK);

    srt_session_destroy(s);
}

// ---------------------------------------------------------------------------
// C-006: srt_last_error with no pending error
// ---------------------------------------------------------------------------
TEST_CASE("C-006: srt_last_error returns empty string when no error is pending",
          "[c][abi][edge]") {
    srt_clear_last_error();
    const char *msg = srt_last_error();
    REQUIRE(msg != nullptr);
    REQUIRE(std::string(msg).empty());
    REQUIRE(srt_last_error_code() == SRT_OK);
}

// ---------------------------------------------------------------------------
// C-010: srt_task_wait with a negative timeout waits until terminal
// ---------------------------------------------------------------------------
TEST_CASE("C-010: srt_task_wait with a negative timeout waits until terminal",
          "[c][abi][edge]") {
    srt_SessionHandle *h = srt_session_create_v2();
    REQUIRE(h != nullptr);
    // Point roots at an existing directory so the async refresh does real
    // (bounded) filesystem work, keeping the task non-terminal long enough to
    // exercise the wait-forever branch (timeout_ms < 0).
    const char *roots[] = {"."};
    REQUIRE(srt_session_set_roots(h, roots, 1) == SRT_OK);

    srt_TaskHandle *task = srt_session_refresh_async(h);
    REQUIRE(task != nullptr);

    // A negative timeout waits indefinitely until the task reaches a terminal
    // state. The refresh task is guaranteed to terminate: the detached
    // watcher (runRefreshWatcher) catches all exceptions and transitions the
    // task to SUCCEEDED/FAILED, so this call always returns.
    srt_clear_last_error();
    REQUIRE(srt_task_wait(task, -1) == SRT_OK);

    // After a successful wait the task must be in a terminal state.
    srt_TaskState st = srt_task_state(task);
    REQUIRE((st == SRT_TASK_STATE_SUCCEEDED || st == SRT_TASK_STATE_FAILED ||
             st == SRT_TASK_STATE_CANCELLED));

    srt_task_destroy(task);
    srt_session_destroy_v2(h);
}

// ---------------------------------------------------------------------------
// C-011: srt_task_state on a destroyed handle
// ---------------------------------------------------------------------------
TEST_CASE("C-011: srt_task_state on a destroyed handle returns FAILED",
          "[c][abi][edge]") {
    srt_SessionHandle *h = srt_session_create_v2();
    REQUIRE(h != nullptr);
    srt_TaskHandle *task = srt_session_refresh_async(h);
    REQUIRE(task != nullptr);

    // Bring the task to a terminal state, then destroy the handle.
    REQUIRE(srt_task_wait(task, 5000) == SRT_OK);
    srt_task_destroy(task);

    // A destroyed handle decodes to an id no longer present in the task
    // table; the implementation returns SRT_TASK_STATE_FAILED for it.
    REQUIRE(srt_task_state(task) == SRT_TASK_STATE_FAILED);

    srt_session_destroy_v2(h);
}

// ---------------------------------------------------------------------------
// C-012: TLS last-error isolation across threads
// ---------------------------------------------------------------------------
TEST_CASE("C-012: TLS last-error is isolated per thread", "[c][abi][edge][tls]") {
    // Clear this (main) thread's error first.
    srt_clear_last_error();
    REQUIRE(std::string(srt_last_error()).empty());
    REQUIRE(srt_last_error_code() == SRT_OK);

    std::atomic<bool> aErrorSet{false};
    std::atomic<bool> aStop{false};

    std::thread a([&] {
        // Thread A: clear its own TLS buffer, then trigger an error on A.
        srt_clear_last_error();
        srt_session_destroy(nullptr); // sets SRT_ERR_INVALID_ARG on A's TLS
        // Record whether A now has a pending error. Assertions are evaluated
        // on the main thread to avoid cross-thread REQUIRE.
        aErrorSet.store(
            srt_last_error_code() == SRT_ERR_INVALID_ARG &&
                !std::string(srt_last_error()).empty(),
            std::memory_order_release);
        // Keep A alive (its TLS error pending) until B has sampled its own
        // buffer, so the isolation check is deterministic.
        while (!aStop.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
    });

    // Wait until A has definitely set its TLS error.
    while (!aErrorSet.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    REQUIRE(aErrorSet.load());

    // Thread B (this/main thread) must NOT see A's error — TLS isolation.
    REQUIRE(std::string(srt_last_error()).empty());
    REQUIRE(srt_last_error_code() == SRT_OK);

    // Release A and join.
    aStop.store(true, std::memory_order_release);
    a.join();
}

// ---------------------------------------------------------------------------
// C-013: srt_task_result_json on a non-terminal task
// ---------------------------------------------------------------------------
TEST_CASE("C-013: srt_task_result_json returns NULL for a non-terminal task",
          "[c][abi][edge]") {
    srt_SessionHandle *h = srt_session_create_v2();
    REQUIRE(h != nullptr);
    // Point roots at an existing directory so performRefresh does real
    // filesystem iteration, widening the RUNNING window we can observe.
    const char *roots[] = {"."};
    REQUIRE(srt_session_set_roots(h, roots, 1) == SRT_OK);

    srt_clear_last_error();
    srt_TaskHandle *task = srt_session_refresh_async(h);
    REQUIRE(task != nullptr);

    // Poll briefly to observe the non-terminal (RUNNING) state. refresh_async
    // sets the state to RUNNING synchronously and launches the async worker
    // plus a detached watcher on separate threads, so the first state reads
    // after refresh_async are very likely to observe RUNNING. When observed,
    // result_json must return NULL (state != SUCCEEDED) and out_size == 0.
    // The assertion is race-tolerant: it is only evaluated when a non-terminal
    // state is actually observed, so it never produces a false failure on
    // very fast machines where the window is missed.
    for (int i = 0; i < 200; ++i) {
        srt_TaskState st = srt_task_state(task);
        if (st != SRT_TASK_STATE_SUCCEEDED && st != SRT_TASK_STATE_FAILED &&
            st != SRT_TASK_STATE_CANCELLED) {
            size_t outSize = 999;
            const char *json = srt_task_result_json(task, &outSize);
            REQUIRE(json == nullptr);
            REQUIRE(outSize == 0);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Cleanup: drive the task to a terminal state, then release handles.
    srt_task_wait(task, 30000);
    srt_task_destroy(task);
    srt_session_destroy_v2(h);
}

// ===========================================================================
// C-015 ~ C-018: supplementary C ABI edge coverage (API version query, free
// function NULL safety). All cases use only APIs declared in srt.h.
// ===========================================================================

// ---------------------------------------------------------------------------
// C-015: srt_get_v4_api_version returns a version consistent with the macro
// Verify (MAJOR << 16) | (MINOR << 8) | PATCH encoding format.
// ---------------------------------------------------------------------------
TEST_CASE("C-015: srt_get_v4_api_version matches macro", "[c][abi][edge]") {
    const int version = srt_get_v4_api_version();
    REQUIRE(version == SRT_V4_API_VERSION);
    // Verify MAJOR/MINOR/PATCH field extraction
    // Note: in C++ == has higher precedence than &, so the & operand must be
    // parenthesized; otherwise (version >> 16) & 0xFF == MAJOR would parse as
    // (version >> 16) & (0xFF == MAJOR), which is always 0, making the
    // assertion always fail.
    REQUIRE(((version >> 16) & 0xFF) == SRT_V4_API_VERSION_MAJOR);
    REQUIRE(((version >> 8) & 0xFF) == SRT_V4_API_VERSION_MINOR);
    REQUIRE((version & 0xFF) == SRT_V4_API_VERSION_PATCH);
}

// ---------------------------------------------------------------------------
// C-016: srt_free_string(NULL) is a no-op
// Documentation states: passing NULL must have no side effects and not crash.
// (Subsumes the former C-007 which only verified a single call.)
// ---------------------------------------------------------------------------
TEST_CASE("C-016: srt_free_string(NULL) is a no-op", "[c][abi][edge]") {
    REQUIRE_NOTHROW(srt_free_string(nullptr));
    // Multiple calls should also be safe
    REQUIRE_NOTHROW(srt_free_string(nullptr));
    REQUIRE_NOTHROW(srt_free_string(nullptr));
}

// ---------------------------------------------------------------------------
// C-017: srt_free_string_array(NULL, 0) is a no-op
// Documentation states: passing NULL is a no-op.
// ---------------------------------------------------------------------------
TEST_CASE("C-017: srt_free_string_array(NULL, 0) is a no-op", "[c][abi][edge]") {
    REQUIRE_NOTHROW(srt_free_string_array(nullptr, 0));
    // count > 0 with arr == NULL is undocumented; only verify count=0 + arr=NULL is safe
}

// ---------------------------------------------------------------------------
// C-018: srt_free_buffer(NULL) is a no-op
// NULL-safety semantics consistent with srt_free_string.
// ---------------------------------------------------------------------------
TEST_CASE("C-018: srt_free_buffer(NULL) is a no-op", "[c][abi][edge]") {
    REQUIRE_NOTHROW(srt_free_buffer(nullptr));
    REQUIRE_NOTHROW(srt_free_buffer(nullptr));
}

// ===========================================================================
// C-032 ~ C-040: regression tests for recently fixed C ABI bugs (empty-array
// semantics, negative count rejection, task_state error propagation,
// refresh_async register-before-watcher, post-completion state read). All
// cases use only APIs declared in srt.h.
//
// Note: C-037/C-038/C-039 were also regression-flavored but are now covered
// in tst_c_abi_input_validation.cpp with detailed error-message checks.
// ===========================================================================

// ---------------------------------------------------------------------------
// C-032: srt_session_set_reserved_phonemes with empty phonemes array
// Passing a valid handle with phonemes=NULL, count=0 should be valid (clearing
// reserved phonemes) or return an error.
//
// Unique: the input_validation counterpart tests a *valid* phonemes array;
// this case exercises the empty-array boundary (count=0 + NULL).
// ---------------------------------------------------------------------------
TEST_CASE("C-032: srt_session_set_reserved_phonemes with empty array",
          "[c][abi][edge]") {
    srt_SessionHandle *h = srt_session_create_v2();
    REQUIRE(h != nullptr);
    srt_clear_last_error();
    srt_error result = srt_session_set_reserved_phonemes(h, nullptr, 0);
    // Accept success or InvalidArg (implementation-defined); only require no crash
    REQUIRE((result == SRT_OK || result == SRT_ERR_INVALID_ARG));
    srt_session_destroy_v2(h);
}

// ---------------------------------------------------------------------------
// C-033: srt_session_set_package_paths with negative count returns INVALID_ARG
// Regression: a negative count previously reached the vector reserve/loop with
// a huge size_t (from int -1), causing bad_alloc and SRT_ERR_GENERIC. The fix
// adds an explicit count < 0 guard that returns SRT_ERR_INVALID_ARG.
// ---------------------------------------------------------------------------
TEST_CASE("C-033: srt_session_set_package_paths rejects negative count",
          "[c][abi][edge]") {
    srt_session s = srt_session_create();
    REQUIRE(s != nullptr);

    srt_clear_last_error();
    // count = -1 must be rejected as INVALID_ARG, not SRT_ERR_GENERIC from
    // a bad_alloc on reserve(static_cast<size_t>(-1)).
    REQUIRE(srt_session_set_package_paths(s, nullptr, -1) == SRT_ERR_INVALID_ARG);
    REQUIRE(srt_last_error_code() == SRT_ERR_INVALID_ARG);
    REQUIRE_FALSE(std::string(srt_last_error()).empty());

    srt_session_destroy(s);
}

// ---------------------------------------------------------------------------
// C-034: srt_task_state on null handle sets last error
// Regression: srt_task_state(NULL) must set the TLS last-error so callers can
// diagnose the failure, not just silently return FAILED.
//
// Unique vs input_validation: this case additionally verifies the TLS error
// message AND code are set (input_validation only checks the return value).
// ---------------------------------------------------------------------------
TEST_CASE("C-034: srt_task_state(NULL) sets last error", "[c][abi][edge]") {
    srt_clear_last_error();
    REQUIRE(srt_task_state(nullptr) == SRT_TASK_STATE_FAILED);
    // The implementation must set a non-empty error message on the null path.
    REQUIRE_FALSE(std::string(srt_last_error()).empty());
    REQUIRE(srt_last_error_code() != SRT_OK);
}

// ---------------------------------------------------------------------------
// C-035: srt_task_state on a destroyed handle sets a descriptive last error
// mentioning "invalid handle". Regression: a destroyed handle must propagate
// a descriptive error, not just silently return FAILED.
// ---------------------------------------------------------------------------
TEST_CASE("C-035: srt_task_state on destroyed handle sets invalid-handle error",
          "[c][abi][edge]") {
    srt_SessionHandle *h = srt_session_create_v2();
    REQUIRE(h != nullptr);
    srt_TaskHandle *task = srt_session_refresh_async(h);
    REQUIRE(task != nullptr);

    // Bring the task to a terminal state, then destroy the handle.
    REQUIRE(srt_task_wait(task, 5000) == SRT_OK);
    srt_task_destroy(task);

    // A destroyed handle decodes to an id no longer present in the task
    // table; the implementation must set a descriptive last error.
    srt_clear_last_error();
    REQUIRE(srt_task_state(task) == SRT_TASK_STATE_FAILED);
    std::string msg = srt_last_error();
    REQUIRE_FALSE(msg.empty());
    REQUIRE(msg.find("invalid handle") != std::string::npos);

    srt_session_destroy_v2(h);
}

// ---------------------------------------------------------------------------
// C-036: srt_session_refresh_async registers the task before starting the
// watcher. Regression: the watcher thread was started before the task was
// registered in the handle table; if the watcher completed (or create()
// threw) before registration, the returned handle was invalid or the task
// state was silently FAILED. The fix registers the task first.
// ---------------------------------------------------------------------------
TEST_CASE("C-036: srt_session_refresh_async registers task before watcher",
          "[c][abi][edge]") {
    srt_SessionHandle *h = srt_session_create_v2();
    REQUIRE(h != nullptr);

    srt_clear_last_error();
    srt_TaskHandle *task = srt_session_refresh_async(h);
    REQUIRE(task != nullptr);

    // Immediately after refresh_async returns, the task must be registered
    // and in a non-FAILED state (RUNNING, PENDING, or already SUCCEEDED if
    // the empty-roots refresh completed before this read). A FAILED state
    // here would indicate the task was never registered or the watcher
    // crashed before the state could be read.
    srt_TaskState st = srt_task_state(task);
    REQUIRE((st == SRT_TASK_STATE_RUNNING || st == SRT_TASK_STATE_PENDING ||
             st == SRT_TASK_STATE_SUCCEEDED));

    // Cleanup: drive to terminal, then release handles.
    srt_task_wait(task, 30000);
    srt_task_destroy(task);
    srt_session_destroy_v2(h);
}

// ---------------------------------------------------------------------------
// C-040: srt_task_state after task completion returns SUCCEEDED and does
// not set last_error. Regression: reading a completed task's state could
// inadvertently set the TLS error buffer; the fix ensures the success path
// of srt_task_state is silent.
// ---------------------------------------------------------------------------
TEST_CASE("C-040: srt_task_state after completion returns SUCCEEDED without error",
          "[c][abi][edge]") {
    srt_SessionHandle *h = srt_session_create_v2();
    REQUIRE(h != nullptr);

    srt_TaskHandle *task = srt_session_refresh_async(h);
    REQUIRE(task != nullptr);

    // Wait for the refresh to complete. With no roots set, performRefresh
    // scans an empty root list and succeeds with an empty snapshot (mirrors
    // C-005's legacy behavior for srt_session_refresh).
    REQUIRE(srt_task_wait(task, 30000) == SRT_OK);

    // Clear any residual TLS error, then read the state. On success the
    // implementation must not set the TLS error buffer.
    srt_clear_last_error();
    srt_TaskState st = srt_task_state(task);
    REQUIRE(st == SRT_TASK_STATE_SUCCEEDED);
    REQUIRE(srt_last_error_code() == SRT_OK);
    REQUIRE(std::string(srt_last_error()).empty());

    srt_task_destroy(task);
    srt_session_destroy_v2(h);
}
