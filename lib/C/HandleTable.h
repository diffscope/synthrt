// HandleTable.h — thread-safe handle table for the vnext C ABI (session/model/task)
//
// Implements the handle table infrastructure described in WP7 of the vnext
// refactoring plan. The table stores weak_ptr entries so that destroying a
// handle only removes the table entry; the underlying object stays alive as
// long as a running task (or other shared_ptr owner) holds a reference.
//
// Design points (see docs/refactoring-vnext/04-...md "最小 C ABI"):
//   - Handle IDs start at 1; 0 is the stable invalid value (NULL pointer).
//   - The C ABI opaque pointer encodes the HandleId directly
//     (reinterpret_cast<HandleId>(ptr)), so after destroy the same pointer
//     value can be passed back to the library and will map to an invalid
//     handle instead of crashing (ROBUST-03 / "destroy 后句柄稳定返回
//     InvalidHandle").
//   - Thread safety: each table guards its map with a mutex.

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <synthrt/C/srt.h>

#include <synthrt/Core/Core/Runtime.h>
#include <synthrt/G2P/LanguageService.h>

#include <diffsinger/Session/VoicebankSession.h>
#include <diffsinger/Session/ModelSetHandle.h>

namespace srt::c_api {

using HandleId = uint64_t;

constexpr HandleId kInvalidHandle = 0;

/// 线程安全的句柄表,管理 C ABI 公开的对象句柄。
/// 句柄 ID 从 1 开始递增,0 表示无效。
/// destroy 后句柄立即失效,内部对象通过 shared_ptr 延迟释放
/// (运行中的 task 持有引用,destroy 不会中断)。
///
/// 表项持有 shared_ptr (强引用),保证 session/model 在 destroy 前始终存活。
/// task 的 watcher 线程通过 shared_ptr 持有 TaskData,destroy 不会中断运行中的 task。
template <typename T>
class HandleTable {
public:
    HandleTable() = default;
    ~HandleTable() = default;

    HandleTable(const HandleTable &) = delete;
    HandleTable &operator=(const HandleTable &) = delete;

    /// 分配新句柄,返回句柄 ID。空指针返回 kInvalidHandle。
    HandleId create(std::shared_ptr<T> obj) {
        if (!obj) {
            return kInvalidHandle;
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        HandleId id = m_nextId.fetch_add(1, std::memory_order_relaxed);
        m_entries.emplace(id, std::move(obj));
        return id;
    }

    /// 查找句柄,返回对象 shared_ptr。无效句柄返回空。
    /// destroy 后的句柄返回空 (表项已移除)。
    std::shared_ptr<T> lookup(HandleId id) const {
        if (id == kInvalidHandle) {
            return nullptr;
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_entries.find(id);
        if (it == m_entries.end()) {
            return nullptr;
        }
        return it->second;
    }

    /// 销毁句柄,移除表项。返回是否成功(句柄存在才成功)。
    /// 注意:已 lookup 的 shared_ptr 仍保持对象存活。
    bool destroy(HandleId id) {
        if (id == kInvalidHandle) {
            return false;
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_entries.find(id);
        if (it == m_entries.end()) {
            return false;
        }
        m_entries.erase(it);
        return true;
    }

    /// 销毁所有句柄(用于 shutdown)。
    void clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_entries.clear();
    }

private:
    mutable std::mutex m_mutex;
    std::unordered_map<HandleId, std::shared_ptr<T>> m_entries;
    std::atomic<HandleId> m_nextId{1};
};

// --------------------------------------------------------------------------
// Handle data structs (internal — not exposed to the C header)
// --------------------------------------------------------------------------
//
// These are the concrete objects backing each opaque handle. WP8 connects
// VoicebankSession (real C++ API) into SessionData, ModelSetHandle into
// ModelData, and shared_future<RefreshResult> into TaskData. WP6 (v3) adds
// RuntimeData / LanguageServiceData for the borrowed-resource session factory.

struct RuntimeData {
    RuntimeData() = default;
    ~RuntimeData() = default;

    // Real Runtime — thread-safe (has its own internal mutex).
    srt::core::Runtime runtime;
};

struct LanguageServiceData {
    LanguageServiceData() = default;
    ~LanguageServiceData() = default;

    // Real LanguageService — thread-safe after initialize().
    srt::g2p::LanguageService languageService;
};

struct SessionData {
    SessionData() = default;
    ~SessionData() = default;

    /// Real VoicebankSession — thread-safe (has its own internal mutex).
    /// Owns the voicebank snapshot, refresh pipeline, and model set factory.
    ds::session::VoicebankSession session;
};

struct ModelData {
    ModelData() = default;
    ~ModelData() = default;

    std::shared_ptr<SessionData> session;
    /// Real ModelSetHandle bound to a snapshot generation. May become stale
    /// after a successful refresh (start() returns StaleModelSet; load/stop/
    /// unload remain usable).
    std::shared_ptr<ds::session::ModelSetHandle> handle;
    std::string packageId;
    std::string singerId;
    std::string version;

    /// ModelBusy 协作:同 model 忙时返回 false (调用方应返回 SRT_ERR_MODEL_BUSY)。
    bool tryAcquire() {
        bool expected = false;
        return m_busy.compare_exchange_strong(expected, true,
                                              std::memory_order_acq_rel);
    }

    void release() {
        m_busy.store(false, std::memory_order_release);
    }

    bool isBusy() const {
        return m_busy.load(std::memory_order_acquire);
    }

private:
    std::atomic<bool> m_busy{false};
};

struct TaskData {
    TaskData() = default;
    ~TaskData() = default;

    /// Keeps the session alive after srt_session_destroy (destroy race safety).
    std::shared_ptr<SessionData> session;

    /// Task kind — determines how result_json is serialized. Only Refresh is
    /// wired in WP8; G2p/S2p/Stage are future task kinds.
    enum class Type { Refresh, G2p, S2p, Stage };
    Type type = Type::Refresh;

    mutable std::mutex mutex;
    std::condition_variable cv;
    srt_TaskState state = SRT_TASK_STATE_PENDING;
    std::string resultJson;
    std::string errorMessage;
    int errorCode = 0;
    bool cancelRequested = false;

    /// Refresh-specific: shared_future returned by VoicebankSession::refreshAsync().
    /// The watcher thread blocks on get() and updates state/resultJson under mutex.
    std::shared_future<ds::session::RefreshResult> refreshFuture;
};

// --------------------------------------------------------------------------
// Global handle table accessors (defined in HandleTable.cpp)
// --------------------------------------------------------------------------

HandleTable<RuntimeData> &runtimeTable();
HandleTable<LanguageServiceData> &languageServiceTable();
HandleTable<SessionData> &sessionTable();
HandleTable<ModelData> &modelTable();
HandleTable<TaskData> &taskTable();

// --------------------------------------------------------------------------
// Handle encode/decode helpers
// --------------------------------------------------------------------------
//
// The C ABI opaque pointer encodes the HandleId directly. This makes destroy
// stable: after destroy the same pointer value maps to a missing entry and
// the caller gets SRT_ERR_INVALID_HANDLE instead of a use-after-free.

inline HandleId decodeRuntimeHandle(const srt_RuntimeHandle *h) {
    return reinterpret_cast<HandleId>(h);
}
inline srt_RuntimeHandle *encodeRuntimeHandle(HandleId id) {
    return reinterpret_cast<srt_RuntimeHandle *>(id);
}

inline HandleId decodeLanguageServiceHandle(const srt_LanguageServiceHandle *h) {
    return reinterpret_cast<HandleId>(h);
}
inline srt_LanguageServiceHandle *encodeLanguageServiceHandle(HandleId id) {
    return reinterpret_cast<srt_LanguageServiceHandle *>(id);
}

inline HandleId decodeSessionHandle(const srt_SessionHandle *h) {
    return reinterpret_cast<HandleId>(h);
}
inline srt_SessionHandle *encodeSessionHandle(HandleId id) {
    return reinterpret_cast<srt_SessionHandle *>(id);
}

inline HandleId decodeModelHandle(const srt_ModelHandle *h) {
    return reinterpret_cast<HandleId>(h);
}
inline srt_ModelHandle *encodeModelHandle(HandleId id) {
    return reinterpret_cast<srt_ModelHandle *>(id);
}

inline HandleId decodeTaskHandle(const srt_TaskHandle *h) {
    return reinterpret_cast<HandleId>(h);
}
inline srt_TaskHandle *encodeTaskHandle(HandleId id) {
    return reinterpret_cast<srt_TaskHandle *>(id);
}

} // namespace srt::c_api
