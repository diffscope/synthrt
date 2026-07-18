// SessionHandleTest.cpp — WP8 C ABI delegation tests for the vnext
// session/model/task handle table.
//
// WP7 verified the handle table infrastructure with stub refresh bodies. WP8
// connects the real ds::session::VoicebankSession pipeline, so the tests now
// exercise:
//   1. destroy → subsequent operations return SRT_ERR_INVALID_HANDLE.
//   2. destroy does not interrupt a running refresh task (task holds shared_ptr).
//   3. refresh is not cancellable (cancel returns OK but task still SUCCEEDED).
//   4. HandleTable is thread-safe under concurrent create/destroy/lookup.
//   5. ModelBusy cooperation via ModelData::tryAcquire/release.
//   6. ErrorCode::ModelBusy maps to SRT_ERR_MODEL_BUSY.
//   7. session refresh end-to-end with a real voicebank fixture.
//   8. model create failure path (no runtime configured → NULL + last_error).

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <synthrt/C/srt.h>

// Internal headers (lib/C is added to the include path by the test target).
#include "HandleTable.h"
#include "LastError.h"

// --------------------------------------------------------------------------
// Voicebank fixture helpers (mirror ds-session unittests/test_voicebank_session.cpp)
// --------------------------------------------------------------------------
namespace {
std::filesystem::path makeRoot() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
                      ("srt-c-abi-" + std::to_string(stamp));
    std::filesystem::create_directories(root / "bank");
    return root;
}

void writeFile(const std::filesystem::path &path, const std::string &text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path) << text;
}

void makePackage(const std::filesystem::path &root) {
    const auto bank = root / "bank";
    writeFile(bank / "desc.json",
              R"({"id":"session.test","version":"1.0.0","contributes":{"singers":["characters/test/config.json"],"inferences":["inferences/duration/config.json"]}})");
    writeFile(bank / "characters/test/config.json",
              R"({"id":"test","imports":[{"inferenceId":"duration"}],"configuration":{"defaultLanguage":"cmn","languages":[{"id":"cmn"}]}})");
    writeFile(bank / "inferences/duration/config.json",
              R"({"id":"duration","class":"ai.svs.DurationInference","configuration":{}})");
}
} // namespace

// --------------------------------------------------------------------------
// 1. destroy → InvalidHandle
// --------------------------------------------------------------------------
TEST_CASE("session handle: destroy makes handle invalid", "[abi][vnext]") {
    srt_SessionHandle *session = srt_session_create_v2();
    REQUIRE(session != nullptr);

    // Operations succeed before destroy.
    const char *roots[] = {"/path/a", "/path/b"};
    REQUIRE(srt_session_set_roots(session, roots, 2) == SRT_OK);

    const char *phonemes[] = {"a", "i", "u"};
    REQUIRE(srt_session_set_reserved_phonemes(session, phonemes, 3) == SRT_OK);

    // Destroy.
    REQUIRE(srt_session_destroy_v2(session) == SRT_OK);

    // Same pointer value now decodes to a missing id → InvalidHandle.
    REQUIRE(srt_session_set_roots(session, roots, 2) == SRT_ERR_INVALID_HANDLE);
    REQUIRE(srt_session_set_reserved_phonemes(session, phonemes, 3) ==
            SRT_ERR_INVALID_HANDLE);
    REQUIRE(srt_session_refresh_async(session) == nullptr);
    REQUIRE(srt_last_error_code() == SRT_ERR_INVALID_HANDLE);
    REQUIRE(srt_session_snapshot(session) == nullptr);

    // Double destroy returns InvalidHandle (not a no-op).
    REQUIRE(srt_session_destroy_v2(session) == SRT_ERR_INVALID_HANDLE);

    // NULL destroy is a no-op (returns SRT_OK).
    REQUIRE(srt_session_destroy_v2(nullptr) == SRT_OK);
}

// --------------------------------------------------------------------------
// 2. destroy does not interrupt a running refresh task
// --------------------------------------------------------------------------
TEST_CASE("session handle: destroy keeps running task alive", "[abi][vnext]") {
    srt_SessionHandle *session = srt_session_create_v2();
    REQUIRE(session != nullptr);

    // Start an async refresh. With no roots the refresh completes quickly
    // (empty snapshot), but the task handle stays valid after session destroy
    // because the task table holds a shared_ptr to TaskData, which in turn
    // holds a shared_ptr to SessionData.
    srt_TaskHandle *task = srt_session_refresh_async(session);
    REQUIRE(task != nullptr);

    // Destroy the session while the task may still be running. The task
    // holds a shared_ptr<SessionData>, so the session object survives.
    REQUIRE(srt_session_destroy_v2(session) == SRT_OK);

    // The task can still be waited on.
    REQUIRE(srt_task_wait(task, 10000) == SRT_OK);
    REQUIRE(srt_task_state(task) == SRT_TASK_STATE_SUCCEEDED);

    // The result JSON is available and reflects the real RefreshResult.
    size_t resultSize = 0;
    const char *result = srt_task_result_json(task, &resultSize);
    REQUIRE(result != nullptr);
    REQUIRE(resultSize > 0);
    REQUIRE(std::strstr(result, "\"succeeded\":true") != nullptr);
    srt_free_buffer(const_cast<char *>(result));

    // Destroy the task. The session data is released once the task's
    // shared_ptr is dropped.
    srt_task_destroy(task);

    // Task operations after destroy report failure.
    REQUIRE(srt_task_state(task) == SRT_TASK_STATE_FAILED);
    REQUIRE(srt_task_wait(task, 100) == SRT_ERR_INVALID_HANDLE);
    REQUIRE(srt_task_cancel(task) == SRT_ERR_INVALID_HANDLE);
}

// --------------------------------------------------------------------------
// 3. Refresh is not cancellable (cancel returns OK but task still SUCCEEDED)
// --------------------------------------------------------------------------
// WP8: refresh delegates to VoicebankSession::refreshAsync which does not
// honor cancellation. cancelRequested is recorded for future task kinds
// (G2P/S2P/stage). The task still transitions to SUCCEEDED.
TEST_CASE("task cancel returns OK but refresh is not cancellable", "[abi][vnext]") {
    srt_SessionHandle *session = srt_session_create_v2();
    REQUIRE(session != nullptr);

    srt_TaskHandle *task = srt_session_refresh_async(session);
    REQUIRE(task != nullptr);

    // Destroy the session first (task holds shared_ptr to SessionData).
    REQUIRE(srt_session_destroy_v2(session) == SRT_OK);

    // Request cancellation. Returns OK, but refresh does not honor it.
    REQUIRE(srt_task_cancel(task) == SRT_OK);

    REQUIRE(srt_task_wait(task, 10000) == SRT_OK);
    // Refresh is not cancellable — task completes normally.
    REQUIRE(srt_task_state(task) == SRT_TASK_STATE_SUCCEEDED);

    // The result is still available.
    size_t resultSize = 0;
    const char *result = srt_task_result_json(task, &resultSize);
    REQUIRE(result != nullptr);
    REQUIRE(std::strstr(result, "\"succeeded\":true") != nullptr);
    srt_free_buffer(const_cast<char *>(result));

    srt_task_destroy(task);
}

// --------------------------------------------------------------------------
// 4. HandleTable thread safety
// --------------------------------------------------------------------------
TEST_CASE("handle table: concurrent create/destroy/lookup", "[abi][vnext]") {
    constexpr int kThreads = 4;
    constexpr int kIterations = 50;

    std::vector<std::thread> threads;
    std::atomic<int> successCount{0};

    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < kIterations; ++i) {
                srt_SessionHandle *h = srt_session_create_v2();
                if (h) {
                    if (srt_session_set_roots(h, nullptr, 0) == SRT_OK) {
                        successCount.fetch_add(1, std::memory_order_relaxed);
                    }
                    srt_session_destroy_v2(h);
                }
            }
        });
    }

    for (auto &t : threads) {
        t.join();
    }

    REQUIRE(successCount.load() == kThreads * kIterations);
}

// --------------------------------------------------------------------------
// 5. ModelBusy cooperation (ModelData inserted directly — no singer needed)
// --------------------------------------------------------------------------
TEST_CASE("model handle: ModelBusy cooperation", "[abi][vnext]") {
    // WP8: srt_model_create now requires a real singer + Runtime, so we
    // insert a ModelData directly into the table to test the ModelBusy
    // mechanism in isolation.
    auto data = std::make_shared<srt::c_api::ModelData>();
    data->packageId = "pkg";
    data->singerId = "singer";
    data->version = "2.0";
    srt::c_api::HandleId id = srt::c_api::modelTable().create(data);
    REQUIRE(id != srt::c_api::kInvalidHandle);

    srt_ModelHandle *model = srt::c_api::encodeModelHandle(id);
    REQUIRE(model != nullptr);

    auto looked = srt::c_api::modelTable().lookup(id);
    REQUIRE(looked != nullptr);

    // First acquire succeeds.
    REQUIRE(looked->tryAcquire() == true);
    REQUIRE(looked->isBusy() == true);

    // Second acquire fails (simulates concurrent stage execution → ModelBusy).
    REQUIRE(looked->tryAcquire() == false);
    REQUIRE(looked->isBusy() == true);

    // Release allows re-acquire.
    looked->release();
    REQUIRE(looked->isBusy() == false);
    REQUIRE(looked->tryAcquire() == true);
    looked->release();

    REQUIRE(srt_model_destroy(model) == SRT_OK);
    REQUIRE(srt_model_destroy(model) == SRT_ERR_INVALID_HANDLE);
}

// --------------------------------------------------------------------------
// 6. ErrorCode::ModelBusy maps to SRT_ERR_MODEL_BUSY
// --------------------------------------------------------------------------
TEST_CASE("error mapping: ModelBusy → SRT_ERR_MODEL_BUSY", "[abi][vnext]") {
    srt::core::Error error(srt::core::ErrorCode::ModelBusy, "model is busy");
    srt_clear_last_error();
    srt_error code = srt::c::detail::mapError(error);
    REQUIRE(code == SRT_ERR_MODEL_BUSY);
    REQUIRE(srt_last_error_code() == SRT_ERR_MODEL_BUSY);
    // The last-error message should mention "model is busy".
    REQUIRE(std::strstr(srt_last_error(), "model is busy") != nullptr);
}

// --------------------------------------------------------------------------
// 7. session refresh end-to-end with a real voicebank fixture
// --------------------------------------------------------------------------
TEST_CASE("session refresh end-to-end with real voicebank", "[abi][vnext][wp8]") {
    const auto root = makeRoot();
    makePackage(root);

    srt_SessionHandle *session = srt_session_create_v2();
    REQUIRE(session != nullptr);

    // Set roots to the temporary voicebank directory.
    const std::string rootStr = root.string();
    const char *roots[] = {rootStr.c_str()};
    REQUIRE(srt_session_set_roots(session, roots, 1) == SRT_OK);

    // Start async refresh.
    srt_TaskHandle *task = srt_session_refresh_async(session);
    REQUIRE(task != nullptr);

    // Wait for completion (refresh of a single small package is fast).
    REQUIRE(srt_task_wait(task, 10000) == SRT_OK);
    REQUIRE(srt_task_state(task) == SRT_TASK_STATE_SUCCEEDED);

    // Verify the result JSON reflects a successful refresh.
    size_t resultSize = 0;
    const char *result = srt_task_result_json(task, &resultSize);
    REQUIRE(result != nullptr);
    REQUIRE(resultSize > 0);
    REQUIRE(std::strstr(result, "\"succeeded\":true") != nullptr);
    // The fixture package contributes one singer.
    REQUIRE(std::strstr(result, "\"singers\":1") != nullptr);
    REQUIRE(std::strstr(result, "\"packages\":1") != nullptr);
    srt_free_buffer(const_cast<char *>(result));

    // Snapshot is now available.
    const void *snap = srt_session_snapshot(session);
    REQUIRE(snap != nullptr);

    srt_task_destroy(task);
    srt_session_destroy_v2(session);
    std::filesystem::remove_all(root);
}

// --------------------------------------------------------------------------
// 8. model create failure path (no runtime configured → NULL + last_error)
// --------------------------------------------------------------------------
TEST_CASE("model create fails without runtime configured", "[abi][vnext][wp8]") {
    const auto root = makeRoot();
    makePackage(root);

    srt_SessionHandle *session = srt_session_create_v2();
    REQUIRE(session != nullptr);

    // Refresh so the singer exists in the snapshot, but do NOT call
    // setRuntime. createModelSet must report InferenceNotInitialized.
    const std::string rootStr = root.string();
    const char *roots[] = {rootStr.c_str()};
    REQUIRE(srt_session_set_roots(session, roots, 1) == SRT_OK);
    srt_TaskHandle *task = srt_session_refresh_async(session);
    REQUIRE(task != nullptr);
    REQUIRE(srt_task_wait(task, 10000) == SRT_OK);
    REQUIRE(srt_task_state(task) == SRT_TASK_STATE_SUCCEEDED);
    srt_task_destroy(task);

    srt_clear_last_error();
    srt_ModelHandle *model = srt_model_create(session, "session.test", "test", "1.0.0");
    REQUIRE(model == nullptr);
    // last_error must be set with a non-OK code.
    REQUIRE(srt_last_error_code() != SRT_OK);

    // Also verify the NULL-argument guards.
    REQUIRE(srt_model_create(nullptr, "p", "s", "1") == nullptr);
    REQUIRE(srt_model_create(session, nullptr, "s", "1") == nullptr);
    REQUIRE(srt_model_create(session, "p", nullptr, "1") == nullptr);

    srt_session_destroy_v2(session);
    std::filesystem::remove_all(root);
}

// --------------------------------------------------------------------------
// 9. model create fails when snapshot is empty (no refresh called)
// --------------------------------------------------------------------------
TEST_CASE("model create fails when snapshot is empty", "[abi][vnext][wp8]") {
    srt_SessionHandle *session = srt_session_create_v2();
    REQUIRE(session != nullptr);

    // No refresh called — snapshot is null. createModelSet returns
    // SessionError, which maps to SRT_ERR_GENERIC.
    srt_clear_last_error();
    srt_ModelHandle *model = srt_model_create(session, "pkg", "singer", "1.0");
    REQUIRE(model == nullptr);
    REQUIRE(srt_last_error_code() != SRT_OK);

    srt_session_destroy_v2(session);
}
