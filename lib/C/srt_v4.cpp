// srt_v4.cpp - synthrt v4 C ABI implementation (FFI layer)
//
// Implements the v4 C ABI declared in <synthrt/C/srt.h>. The implementation
// composes ds::bank::VoicebankScanner + srt::g2p::LanguageService +
// srt::core::Runtime internally (ARCH-03), replacing the former delegation to
// ds::session::DiffSingerSession which was removed in v2 Phase 1. Errors are
// propagated via the shared TLS error buffer and converted to the v4 srt_error
// enum.
//
// The TLS error buffer public entry points (srt_last_error / srt_clear_last_error)
// and the string ownership helpers (srt_free_string / srt_free_string_array) are
// v4-only utilities implemented in LastError.cpp.

#include <synthrt/C/srt.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <diffsinger/Bank/SingerRef.h>
#include <diffsinger/Bank/VoicebankScanner.h>
#include <diffsinger/Session/VoicebankSession.h>
#include <synthrt/G2P/LanguageService.h>
#include <synthrt/Core/Core/Runtime.h>

#include <synthrt/Core/Support/Error.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Core/Support/Logging.h>

#include "LastError.h"
#include "HandleTable.h"

static srt::core::LogCategory cApiLog("c_api");

// --------------------------------------------------------------------------
// Session handle — composes VoicebankScanner + LanguageService + Runtime
// --------------------------------------------------------------------------
//
// Replaces the former ds::session::DiffSingerSession delegation. The session
// owns a VoicebankScanner for bank scanning, a LanguageService for G2P route
// resolution, and a Runtime for plugin/package management. The current C ABI
// surface (5 session functions) only exercises VoicebankScanner; the other
// components are held for future C ABI expansion.
struct SrtSession {
    ds::bank::VoicebankScanner scanner;
    srt::g2p::LanguageService langSvc;
    srt::core::Runtime runtime;
};

static inline SrtSession *toSession(srt_session session) {
    return reinterpret_cast<SrtSession *>(session);
}

static inline srt_session fromSession(SrtSession *session) {
    return reinterpret_cast<srt_session>(session);
}

// --------------------------------------------------------------------------
// ABI version
// --------------------------------------------------------------------------
extern "C" int srt_get_v4_api_version(void) {
    return SRT_V4_API_VERSION;
}

// --------------------------------------------------------------------------
// TLS error buffer (public C API)
// --------------------------------------------------------------------------
extern "C" const char *srt_last_error(void) {
    return srt::c::detail::lastErrorMessage();
}

extern "C" srt_error srt_last_error_code(void) {
    return srt::c::detail::lastErrorCode();
}

extern "C" void srt_clear_last_error(void) {
    srt::c::detail::clearLastError();
}

// --------------------------------------------------------------------------
// String ownership helpers
// --------------------------------------------------------------------------
extern "C" void srt_free_string(char *str) {
    if (str) {
        std::free(str);
    }
}

extern "C" void srt_free_string_array(char **arr, size_t count) {
    if (!arr) {
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        if (arr[i]) {
            std::free(arr[i]);
        }
    }
    std::free(arr);
}

// --------------------------------------------------------------------------
// Runtime lifecycle (no-op by design)
// --------------------------------------------------------------------------
extern "C" srt_error srt_init(void) {
    return SRT_OK;
}

extern "C" srt_error srt_shutdown(void) {
    return SRT_OK;
}

// --------------------------------------------------------------------------
// Session lifecycle
// --------------------------------------------------------------------------
extern "C" srt_session srt_session_create(void) {
    try {
        auto *session = new (std::nothrow) SrtSession();
        if (!session) {
            srt::c::detail::setLastError("srt_session_create: out of memory",
                                         SRT_ERR_OUT_OF_MEM);
            return nullptr;
        }
        return fromSession(session);
    } catch (const std::exception &e) {
        srt::c::detail::setLastError(std::string("srt_session_create: ") + e.what());
        return nullptr;
    }
}

extern "C" srt_error srt_session_destroy(srt_session session) {
    if (!session) {
        srt::c::detail::setLastError("srt_session_destroy: session handle is null",
                                     SRT_ERR_INVALID_ARG);
        return SRT_ERR_INVALID_ARG;
    }
    try {
        delete toSession(session);
        return SRT_OK;
    } catch (const std::exception &e) {
        srt::c::detail::setLastError(std::string("srt_session_destroy: ") + e.what());
        return SRT_ERR_GENERIC;
    }
}

extern "C" srt_error srt_session_set_package_paths(srt_session session,
                                                    const char *const *paths,
                                                    int count) {
    if (!session) {
        srt::c::detail::setLastError("srt_session_set_package_paths: session handle is null",
                                     SRT_ERR_INVALID_ARG);
        return SRT_ERR_INVALID_ARG;
    }
    if (count > 0 && !paths) {
        srt::c::detail::setLastError("srt_session_set_package_paths: paths is null",
                                     SRT_ERR_INVALID_ARG);
        return SRT_ERR_INVALID_ARG;
    }
    if (count < 0) {
        srt::c::detail::setLastError("srt_session_set_package_paths: count is negative",
                                     SRT_ERR_INVALID_ARG);
        return SRT_ERR_INVALID_ARG;
    }

    try {
        std::vector<std::filesystem::path> pathVec;
        pathVec.reserve(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i) {
            if (!paths[i]) {
                srt::c::detail::setLastError("srt_session_set_package_paths: path entry is null",
                                             SRT_ERR_INVALID_ARG);
                return SRT_ERR_INVALID_ARG;
            }
            pathVec.emplace_back(std::filesystem::path(paths[i]));
        }

        toSession(session)->scanner.setSearchPaths(pathVec);
        return SRT_OK;
    } catch (const std::exception &e) {
        srt::c::detail::setLastError(std::string("srt_session_set_package_paths: ") + e.what());
        return SRT_ERR_GENERIC;
    }
}

extern "C" srt_error srt_session_refresh(srt_session session) {
    if (!session) {
        srt::c::detail::setLastError("srt_session_refresh: session handle is null",
                                     SRT_ERR_INVALID_ARG);
        return SRT_ERR_INVALID_ARG;
    }
    try {
        auto result = toSession(session)->scanner.refresh();
        if (!result.hasValue()) {
            return srt::c::detail::mapError(result.takeError());
        }
        return SRT_OK;
    } catch (const std::exception &e) {
        srt::c::detail::setLastError(std::string("srt_session_refresh: ") + e.what());
        return SRT_ERR_GENERIC;
    }
}

// ==========================================================================
// vnext: session/model/task handle table C ABI (WP8 — real delegation)
// ==========================================================================
//
// The vnext functions below use the srt::c_api handle table to manage
// session/model/task objects. WP8 connects the real ds::session::
// VoicebankSession / ModelSetHandle / RefreshResult pipeline. Every extern
// "C" entry point is guarded by try-catch(std::exception) per CODING-02 and
// converts failures into srt_error codes + TLS last-error messages
// (ROBUST-02).
namespace {

using srt::c_api::HandleId;
using srt::c_api::kInvalidHandle;
using srt::c_api::RuntimeData;
using srt::c_api::LanguageServiceData;
using srt::c_api::SessionData;
using srt::c_api::ModelData;
using srt::c_api::TaskData;
using srt::c_api::runtimeTable;
using srt::c_api::languageServiceTable;
using srt::c_api::sessionTable;
using srt::c_api::modelTable;
using srt::c_api::taskTable;
using srt::c_api::decodeRuntimeHandle;
using srt::c_api::encodeRuntimeHandle;
using srt::c_api::decodeLanguageServiceHandle;
using srt::c_api::encodeLanguageServiceHandle;
using srt::c_api::decodeSessionHandle;
using srt::c_api::encodeSessionHandle;
using srt::c_api::decodeModelHandle;
using srt::c_api::encodeModelHandle;
using srt::c_api::decodeTaskHandle;
using srt::c_api::encodeTaskHandle;

// --------------------------------------------------------------------------
// JSON helpers (manual serialization — flat structure, no extra dependency)
// --------------------------------------------------------------------------

// Escapes a string for inclusion in a JSON string literal (without the
// surrounding quotes).
std::string escapeJsonString(const std::string &s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x",
                              static_cast<unsigned char>(c));
                out += buf;
            } else {
                out += c;
            }
            break;
        }
    }
    return out;
}

// Serializes a RefreshResult to a versioned JSON object. Schema (v1):
//   {"succeeded":bool,"changed":bool,"generation":N,"singers":N,
//    "packages":N,"diagnostics":[{"code":N,"message":"..."}],
//    "errorMessage":"..."}
std::string serializeRefreshResult(const ds::session::RefreshResult &r) {
    std::string json;
    json += "{";
    json += "\"succeeded\":";
    json += (r.succeeded ? "true" : "false");
    json += ",\"changed\":";
    json += (r.changed ? "true" : "false");
    if (r.snapshot) {
        json += ",\"generation\":" + std::to_string(r.snapshot->generation);
        json += ",\"singers\":" + std::to_string(r.snapshot->singers.size());
        json += ",\"packages\":" + std::to_string(r.snapshot->packages.size());
    } else {
        json += ",\"generation\":0";
        json += ",\"singers\":0";
        json += ",\"packages\":0";
    }
    json += ",\"diagnostics\":[";
    for (size_t i = 0; i < r.diagnostics.size(); ++i) {
        if (i) json += ",";
        json += "{\"code\":";
        json += std::to_string(static_cast<int>(r.diagnostics[i].code));
        json += ",\"message\":\"";
        json += escapeJsonString(r.diagnostics[i].message);
        json += "\"}";
    }
    json += "]";
    json += ",\"errorMessage\":\"";
    json += escapeJsonString(r.errorMessage);
    json += "\"";
    json += "}";
    return json;
}

// Watcher thread body for a refresh task. Blocks on the shared_future, then
// updates the TaskData state + resultJson under the mutex. Refresh is not
// cancellable (cancelRequested is recorded but not honored — future G2P/S2P/
// stage tasks may honor it). The thread captures a weak_ptr; if the task is
// already gone it does nothing.
void runRefreshWatcher(std::weak_ptr<TaskData> weak) {
    auto t = weak.lock();
    if (!t) return;
    try {
        const auto result = t->m_refreshFuture.get();
        std::lock_guard<std::mutex> lock(t->m_mutex);
        t->m_resultJson = serializeRefreshResult(result);
        t->m_errorMessage.clear();
        t->m_errorCode = 0;
        t->m_state = SRT_TASK_STATE_SUCCEEDED;
        t->m_cv.notify_all();
    } catch (const std::exception &e) {
        std::lock_guard<std::mutex> lock(t->m_mutex);
        t->m_errorMessage = e.what();
        t->m_errorCode = static_cast<int>(SRT_ERR_GENERIC);
        t->m_resultJson.clear();
        t->m_state = SRT_TASK_STATE_FAILED;
        t->m_cv.notify_all();
    } catch (...) {
        std::lock_guard<std::mutex> lock(t->m_mutex);
        t->m_errorMessage = "unknown refresh failure";
        t->m_errorCode = static_cast<int>(SRT_ERR_GENERIC);
        t->m_resultJson.clear();
        t->m_state = SRT_TASK_STATE_FAILED;
        t->m_cv.notify_all();
    }
}

} // namespace

// --------------------------------------------------------------------------
// Session (vnext)
// --------------------------------------------------------------------------
extern "C" srt_SessionHandle *srt_session_create_v2(void) {
    try {
        auto data = std::make_shared<SessionData>();
        HandleId id = sessionTable().create(data);
        if (id == kInvalidHandle) {
            srt::c::detail::setLastError("srt_session_create_v2: out of memory",
                                         SRT_ERR_OUT_OF_MEM);
            return nullptr;
        }
        return encodeSessionHandle(id);
    } catch (const std::exception &e) {
        srt::c::detail::setLastError(std::string("srt_session_create_v2: ") + e.what());
        return nullptr;
    }
}

extern "C" srt_error srt_session_destroy_v2(srt_SessionHandle *handle) {
    if (!handle) {
        return SRT_OK; // no-op, matches the legacy destroy contract
    }
    try {
        HandleId id = decodeSessionHandle(handle);
        if (!sessionTable().destroy(id)) {
            srt::c::detail::setLastError("srt_session_destroy_v2: invalid handle",
                                         SRT_ERR_INVALID_HANDLE);
            return SRT_ERR_INVALID_HANDLE;
        }
        return SRT_OK;
    } catch (const std::exception &e) {
        srt::c::detail::setLastError(std::string("srt_session_destroy_v2: ") + e.what());
        return SRT_ERR_GENERIC;
    }
}

// v3 (WP6): borrowed-resource session factory. The session borrows the Runtime
// and LanguageService via the existing bare setters; WP3 will replace this
// body with the SessionResources injection constructor. Resources are
// caller-owned and must outlive the session.
extern "C" srt_SessionHandle *srt_session_create_with_resources(
    srt_RuntimeHandle *runtime, srt_LanguageServiceHandle *languageService) {
    if (!runtime || !languageService) {
        srt::c::detail::setLastError(
            "srt_session_create_with_resources: null resource handle",
            SRT_ERR_INVALID_ARG);
        return nullptr;
    }
    try {
        HandleId rtId = decodeRuntimeHandle(runtime);
        auto rtData = runtimeTable().lookup(rtId);
        if (!rtData) {
            srt::c::detail::setLastError(
                "srt_session_create_with_resources: invalid runtime handle",
                SRT_ERR_INVALID_HANDLE);
            return nullptr;
        }
        HandleId langId = decodeLanguageServiceHandle(languageService);
        auto langData = languageServiceTable().lookup(langId);
        if (!langData) {
            srt::c::detail::setLastError(
                "srt_session_create_with_resources: invalid language service handle",
                SRT_ERR_INVALID_HANDLE);
            return nullptr;
        }
        // Build SessionData with default-constructed VoicebankSession, then
        // inject resources via the existing bare setters (WP3 will replace
        // this with the SessionResources injection constructor).
        auto data = std::make_shared<SessionData>();
        // Aliasing shared_ptr: non-owning. Runtime/LanguageService outlive
        // the session (caller-owned handles). This mirrors the ds-editor-lite
        // pattern documented in 03-session-and-snapshot.md §1.1.
        std::shared_ptr<srt::g2p::LanguageService> langAlias(
            std::shared_ptr<srt::g2p::LanguageService>{}, &langData->m_languageService);
        data->m_session.setLanguageService(langAlias);
        data->m_session.setRuntime(&rtData->m_runtime);
        HandleId id = sessionTable().create(data);
        if (id == kInvalidHandle) {
            srt::c::detail::setLastError(
                "srt_session_create_with_resources: out of memory",
                SRT_ERR_OUT_OF_MEM);
            return nullptr;
        }
        return encodeSessionHandle(id);
    } catch (const std::exception &e) {
        srt::c::detail::setLastError(
            std::string("srt_session_create_with_resources: ") + e.what());
        return nullptr;
    }
}

extern "C" srt_error srt_session_set_roots(srt_SessionHandle *handle,
                                           const char *const *roots, size_t count) {
    if (!handle) {
        srt::c::detail::setLastError("srt_session_set_roots: handle is null",
                                     SRT_ERR_INVALID_HANDLE);
        return SRT_ERR_INVALID_HANDLE;
    }
    if (count > 0 && !roots) {
        srt::c::detail::setLastError("srt_session_set_roots: roots is null",
                                     SRT_ERR_INVALID_ARG);
        return SRT_ERR_INVALID_ARG;
    }
    try {
        HandleId id = decodeSessionHandle(handle);
        auto data = sessionTable().lookup(id);
        if (!data) {
            srt::c::detail::setLastError("srt_session_set_roots: invalid handle",
                                         SRT_ERR_INVALID_HANDLE);
            return SRT_ERR_INVALID_HANDLE;
        }
        std::vector<std::filesystem::path> pathVec;
        pathVec.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            if (!roots[i]) {
                srt::c::detail::setLastError("srt_session_set_roots: null entry",
                                             SRT_ERR_INVALID_ARG);
                return SRT_ERR_INVALID_ARG;
            }
            pathVec.emplace_back(std::filesystem::path(roots[i]));
        }
        // VoicebankSession::setRoots is thread-safe (internal mutex).
        data->m_session.setRoots(std::move(pathVec));
        return SRT_OK;
    } catch (const std::exception &e) {
        srt::c::detail::setLastError(std::string("srt_session_set_roots: ") + e.what());
        return SRT_ERR_GENERIC;
    }
}

extern "C" srt_error srt_session_set_reserved_phonemes(srt_SessionHandle *handle,
                                                       const char *const *phonemes,
                                                       size_t count) {
    if (!handle) {
        srt::c::detail::setLastError("srt_session_set_reserved_phonemes: handle is null",
                                     SRT_ERR_INVALID_HANDLE);
        return SRT_ERR_INVALID_HANDLE;
    }
    if (count > 0 && !phonemes) {
        srt::c::detail::setLastError("srt_session_set_reserved_phonemes: phonemes is null",
                                     SRT_ERR_INVALID_ARG);
        return SRT_ERR_INVALID_ARG;
    }
    try {
        HandleId id = decodeSessionHandle(handle);
        auto data = sessionTable().lookup(id);
        if (!data) {
            srt::c::detail::setLastError("srt_session_set_reserved_phonemes: invalid handle",
                                         SRT_ERR_INVALID_HANDLE);
            return SRT_ERR_INVALID_HANDLE;
        }
        std::vector<std::string> tmp;
        tmp.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            if (!phonemes[i]) {
                srt::c::detail::setLastError("srt_session_set_reserved_phonemes: null entry",
                                             SRT_ERR_INVALID_ARG);
                return SRT_ERR_INVALID_ARG;
            }
            tmp.emplace_back(phonemes[i]);
        }
        // VoicebankSession::setReservedPhonemes is thread-safe (internal mutex).
        data->m_session.setReservedPhonemes(std::move(tmp));
        return SRT_OK;
    } catch (const std::exception &e) {
        srt::c::detail::setLastError(std::string("srt_session_set_reserved_phonemes: ") + e.what());
        return SRT_ERR_GENERIC;
    }
}

extern "C" srt_TaskHandle *srt_session_refresh_async(srt_SessionHandle *handle) {
    if (!handle) {
        srt::c::detail::setLastError("srt_session_refresh_async: handle is null",
                                     SRT_ERR_INVALID_HANDLE);
        return nullptr;
    }
    try {
        HandleId id = decodeSessionHandle(handle);
        auto session = sessionTable().lookup(id);
        if (!session) {
            srt::c::detail::setLastError("srt_session_refresh_async: invalid handle",
                                         SRT_ERR_INVALID_HANDLE);
            return nullptr;
        }
        auto task = std::make_shared<TaskData>();
        task->m_session = session; // keep the session alive after destroy
        task->m_type = TaskData::Type::Refresh;
        task->m_state = SRT_TASK_STATE_RUNNING;
        // Register the task in the handle table BEFORE starting the watcher
        // thread, so that if create() throws (bad_alloc), no detached thread
        // is left running with an unregistered task (silent success).
        // Note: create() only returns kInvalidHandle for null input, which
        // can't happen here (task is from make_shared), so the kInvalidHandle
        // check below is defensive only — the real OOM path is the catch block.
        HandleId tid = taskTable().create(task);
        // VoicebankSession::refreshAsync coalesces concurrent calls and
        // returns a shared_future. The watcher thread blocks on get() and
        // publishes the result under the task mutex.
        task->m_refreshFuture = session->m_session.refreshAsync();
        // Detached watcher: holds a shared_ptr (via weak.lock()) until the
        // refresh completes, so the task survives srt_task_destroy. Refresh
        // is not cancellable; cancelRequested is recorded for future task
        // kinds (G2P/S2P/stage).
        std::thread(runRefreshWatcher, std::weak_ptr<TaskData>(task)).detach();
        return encodeTaskHandle(tid);
    } catch (const std::exception &e) {
        srt::c::detail::setLastError(std::string("srt_session_refresh_async: ") + e.what());
        return nullptr;
    }
}

extern "C" const void *srt_session_snapshot(srt_SessionHandle *handle) {
    if (!handle) {
        srt::c::detail::setLastError("srt_session_snapshot: handle is null",
                                     SRT_ERR_INVALID_HANDLE);
        return nullptr;
    }
    try {
        HandleId id = decodeSessionHandle(handle);
        auto data = sessionTable().lookup(id);
        if (!data) {
            srt::c::detail::setLastError("srt_session_snapshot: invalid handle",
                                         SRT_ERR_INVALID_HANDLE);
            return nullptr;
        }
        // Returns the immutable snapshot's raw pointer. The snapshot is kept
        // alive by the session's internal shared_ptr<const VoicebankSnapshot>.
        // The raw pointer is valid only until the next refresh publishes a new
        // snapshot; callers must not hold it across tasks (see srt.h).
        auto snap = data->m_session.snapshot();
        if (!snap) {
            return nullptr; // no snapshot yet (refresh not called)
        }
        // Release the local shared_ptr; the session still holds a reference,
        // so the raw pointer remains valid until the next refresh.
        return reinterpret_cast<const void *>(snap.get());
    } catch (const std::exception &e) {
        srt::c::detail::setLastError(std::string("srt_session_snapshot: ") + e.what());
        return nullptr;
    }
}

// --------------------------------------------------------------------------
// Runtime / LanguageService resource handles (v3 / WP6)
// --------------------------------------------------------------------------
//
// Caller-owned handles backing a Runtime / LanguageService instance. Borrowed
// by sessions created via srt_session_create_with_resources. Each create
// allocates a fresh entry in the corresponding table; destroy is idempotent
// and stable (after destroy the handle decodes to an invalid id).

extern "C" srt_RuntimeHandle *srt_runtime_create(void) {
    try {
        auto data = std::make_shared<RuntimeData>();
        HandleId id = runtimeTable().create(data);
        if (id == kInvalidHandle) {
            srt::c::detail::setLastError("srt_runtime_create: out of memory",
                                         SRT_ERR_OUT_OF_MEM);
            return nullptr;
        }
        return encodeRuntimeHandle(id);
    } catch (const std::exception &e) {
        srt::c::detail::setLastError(std::string("srt_runtime_create: ") + e.what());
        return nullptr;
    }
}

extern "C" srt_error srt_runtime_destroy(srt_RuntimeHandle *handle) {
    if (!handle) {
        return SRT_OK; // no-op, matches the session destroy contract
    }
    try {
        HandleId id = decodeRuntimeHandle(handle);
        if (!runtimeTable().destroy(id)) {
            srt::c::detail::setLastError("srt_runtime_destroy: invalid handle",
                                         SRT_ERR_INVALID_HANDLE);
            return SRT_ERR_INVALID_HANDLE;
        }
        return SRT_OK;
    } catch (const std::exception &e) {
        srt::c::detail::setLastError(std::string("srt_runtime_destroy: ") + e.what());
        return SRT_ERR_GENERIC;
    }
}

extern "C" srt_LanguageServiceHandle *srt_language_service_create(void) {
    try {
        auto data = std::make_shared<LanguageServiceData>();
        HandleId id = languageServiceTable().create(data);
        if (id == kInvalidHandle) {
            srt::c::detail::setLastError("srt_language_service_create: out of memory",
                                         SRT_ERR_OUT_OF_MEM);
            return nullptr;
        }
        return encodeLanguageServiceHandle(id);
    } catch (const std::exception &e) {
        srt::c::detail::setLastError(
            std::string("srt_language_service_create: ") + e.what());
        return nullptr;
    }
}

extern "C" srt_error srt_language_service_destroy(srt_LanguageServiceHandle *handle) {
    if (!handle) {
        return SRT_OK; // no-op, matches the session destroy contract
    }
    try {
        HandleId id = decodeLanguageServiceHandle(handle);
        if (!languageServiceTable().destroy(id)) {
            srt::c::detail::setLastError(
                "srt_language_service_destroy: invalid handle",
                SRT_ERR_INVALID_HANDLE);
            return SRT_ERR_INVALID_HANDLE;
        }
        return SRT_OK;
    } catch (const std::exception &e) {
        srt::c::detail::setLastError(
            std::string("srt_language_service_destroy: ") + e.what());
        return SRT_ERR_GENERIC;
    }
}

// --------------------------------------------------------------------------
// Model (vnext)
// --------------------------------------------------------------------------
extern "C" srt_ModelHandle *srt_model_create(srt_SessionHandle *session,
                                             const char *packageId,
                                             const char *singerId,
                                             const char *version) {
    if (!session) {
        srt::c::detail::setLastError("srt_model_create: session handle is null",
                                     SRT_ERR_INVALID_HANDLE);
        return nullptr;
    }
    if (!packageId || !singerId) {
        srt::c::detail::setLastError("srt_model_create: packageId/singerId is null",
                                     SRT_ERR_INVALID_ARG);
        return nullptr;
    }
    try {
        HandleId sid = decodeSessionHandle(session);
        auto sessionData = sessionTable().lookup(sid);
        if (!sessionData) {
            srt::c::detail::setLastError("srt_model_create: invalid session handle",
                                         SRT_ERR_INVALID_HANDLE);
            return nullptr;
        }
        // Build the singer key and delegate to VoicebankSession::createModelSet.
        // On failure the Expected error is mapped to an srt_error code and
        // stored in the TLS last-error buffer (ROBUST-01/ROBUST-02).
        ds::bank::SingerRef ref(packageId, singerId, version ? version : "");
        auto exp = sessionData->m_session.createModelSet(ref);
        if (!exp.hasValue()) {
            srt::c::detail::mapError(exp.takeError());
            return nullptr;
        }
        auto model = std::make_shared<ModelData>();
        model->m_session = sessionData;
        model->m_handle = std::move(exp.take());
        model->m_packageId = packageId;
        model->m_singerId = singerId;
        model->m_version = version ? version : "";
        HandleId id = modelTable().create(model);
        if (id == kInvalidHandle) {
            srt::c::detail::setLastError("srt_model_create: out of memory",
                                         SRT_ERR_OUT_OF_MEM);
            return nullptr;
        }
        return encodeModelHandle(id);
    } catch (const std::exception &e) {
        srt::c::detail::setLastError(std::string("srt_model_create: ") + e.what());
        return nullptr;
    }
}

extern "C" srt_error srt_model_destroy(srt_ModelHandle *handle) {
    if (!handle) {
        return SRT_OK;
    }
    try {
        HandleId id = decodeModelHandle(handle);
        if (!modelTable().destroy(id)) {
            srt::c::detail::setLastError("srt_model_destroy: invalid handle",
                                         SRT_ERR_INVALID_HANDLE);
            return SRT_ERR_INVALID_HANDLE;
        }
        return SRT_OK;
    } catch (const std::exception &e) {
        srt::c::detail::setLastError(std::string("srt_model_destroy: ") + e.what());
        return SRT_ERR_GENERIC;
    }
}

// --------------------------------------------------------------------------
// Task (vnext)
// --------------------------------------------------------------------------
extern "C" srt_TaskState srt_task_state(srt_TaskHandle *handle) {
    if (!handle) {
        srt::c::detail::setLastError("srt_task_state: handle is null",
                                     SRT_ERR_INVALID_HANDLE);
        return SRT_TASK_STATE_FAILED;
    }
    try {
        HandleId id = decodeTaskHandle(handle);
        auto task = taskTable().lookup(id);
        if (!task) {
            srt::c::detail::setLastError("srt_task_state: invalid handle",
                                         SRT_ERR_INVALID_HANDLE);
            return SRT_TASK_STATE_FAILED;
        }
        std::lock_guard<std::mutex> lock(task->m_mutex);
        return task->m_state;
    } catch (...) {
        srt::c::detail::setLastError("srt_task_state: unknown exception");
        return SRT_TASK_STATE_FAILED;
    }
}

extern "C" srt_error srt_task_wait(srt_TaskHandle *handle, int timeout_ms) {
    if (!handle) {
        srt::c::detail::setLastError("srt_task_wait: handle is null",
                                     SRT_ERR_INVALID_HANDLE);
        return SRT_ERR_INVALID_HANDLE;
    }
    try {
        HandleId id = decodeTaskHandle(handle);
        auto task = taskTable().lookup(id);
        if (!task) {
            srt::c::detail::setLastError("srt_task_wait: invalid handle",
                                         SRT_ERR_INVALID_HANDLE);
            return SRT_ERR_INVALID_HANDLE;
        }
        std::unique_lock<std::mutex> lock(task->m_mutex);
        bool terminal = (task->m_state == SRT_TASK_STATE_SUCCEEDED ||
                         task->m_state == SRT_TASK_STATE_FAILED ||
                         task->m_state == SRT_TASK_STATE_CANCELLED);
        if (!terminal) {
            auto pred = [&] {
                return task->m_state == SRT_TASK_STATE_SUCCEEDED ||
                       task->m_state == SRT_TASK_STATE_FAILED ||
                       task->m_state == SRT_TASK_STATE_CANCELLED;
            };
            if (timeout_ms < 0) {
                task->m_cv.wait(lock, pred);
            } else {
                if (!task->m_cv.wait_for(lock,
                                       std::chrono::milliseconds(timeout_ms), pred)) {
                    return SRT_ERR_TIMEOUT;
                }
            }
        }
        return SRT_OK;
    } catch (const std::exception &e) {
        srt::c::detail::setLastError(std::string("srt_task_wait: ") + e.what());
        return SRT_ERR_GENERIC;
    }
}

extern "C" srt_error srt_task_cancel(srt_TaskHandle *handle) {
    if (!handle) {
        srt::c::detail::setLastError("srt_task_cancel: handle is null",
                                     SRT_ERR_INVALID_HANDLE);
        return SRT_ERR_INVALID_HANDLE;
    }
    try {
        HandleId id = decodeTaskHandle(handle);
        auto task = taskTable().lookup(id);
        if (!task) {
            srt::c::detail::setLastError("srt_task_cancel: invalid handle",
                                         SRT_ERR_INVALID_HANDLE);
            return SRT_ERR_INVALID_HANDLE;
        }
        {
            std::lock_guard<std::mutex> lock(task->m_mutex);
            task->m_cancelRequested = true;
            // Cooperative: the worker transitions to CANCELLED when it observes
            // the flag. If already terminal, leave the state as-is.
        }
        return SRT_OK;
    } catch (const std::exception &e) {
        srt::c::detail::setLastError(std::string("srt_task_cancel: ") + e.what());
        return SRT_ERR_GENERIC;
    }
}

extern "C" void srt_task_destroy(srt_TaskHandle *handle) {
    if (!handle) {
        return;
    }
    try {
        HandleId id = decodeTaskHandle(handle);
        taskTable().destroy(id);
        // shared_ptr refs held by running watcher threads keep TaskData alive
        // until the refresh completes; destroying here only drops the table's
        // strong reference. The task handle becomes invalid immediately.
    } catch (const std::exception &e) {
        // destroy must not throw across the extern "C" boundary.
        const std::string msg = std::string("srt_task_destroy: ") + e.what();
        srt::c::detail::setLastError(msg);
        cApiLog.srtWarning(msg);
    } catch (...) {
        // destroy must not throw across the extern "C" boundary.
        srt::c::detail::setLastError("srt_task_destroy: unknown exception");
        cApiLog.srtWarning("srt_task_destroy: unknown exception swallowed");
    }
}

extern "C" const char *srt_task_result_json(srt_TaskHandle *handle, size_t *out_size) {
    if (out_size) {
        *out_size = 0;
    }
    if (!handle) {
        srt::c::detail::setLastError("srt_task_result_json: handle is null",
                                     SRT_ERR_INVALID_HANDLE);
        return nullptr;
    }
    try {
        HandleId id = decodeTaskHandle(handle);
        auto task = taskTable().lookup(id);
        if (!task) {
            srt::c::detail::setLastError("srt_task_result_json: invalid handle",
                                         SRT_ERR_INVALID_HANDLE);
            return nullptr;
        }
        std::lock_guard<std::mutex> lock(task->m_mutex);
        if (task->m_state != SRT_TASK_STATE_SUCCEEDED || task->m_resultJson.empty()) {
            return nullptr;
        }
        // Allocate a fresh copy the caller frees with srt_free_buffer.
        size_t len = task->m_resultJson.size();
        char *buf = static_cast<char *>(std::malloc(len + 1));
        if (!buf) {
            srt::c::detail::setLastError("srt_task_result_json: out of memory",
                                         SRT_ERR_OUT_OF_MEM);
            return nullptr;
        }
        std::memcpy(buf, task->m_resultJson.data(), len);
        buf[len] = '\0';
        if (out_size) {
            *out_size = len;
        }
        return buf;
    } catch (const std::exception &e) {
        srt::c::detail::setLastError(std::string("srt_task_result_json: ") + e.what());
        return nullptr;
    }
}

extern "C" void srt_free_buffer(void *ptr) {
    if (ptr) {
        std::free(ptr);
    }
}
