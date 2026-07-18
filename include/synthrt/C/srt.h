// srt.h - synthrt v4 C ABI (FFI layer)
//
// This is the public v4 C entry point to the synthrt runtime. It is a thin
// runtime-scoped FFI wrapper that composes VoicebankScanner + LanguageService +
// Runtime internally. Handles own runtime instances; there are no global
// singletons or global registries.
//
// The legacy P5 C ABI (srt-c.h) has been removed in v9. This header is the
// sole C ABI surface for synthrt.
//
// \see docs/refactoring-v4/04-plugin-abi-contract.md section 4
// \see docs/refactoring-v4/02-module-contracts.md section 8

#pragma once

#include <synthrt/C/srt_c_global.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ===== ABI version ===== */
#define SRT_V4_API_VERSION_MAJOR 1
#define SRT_V4_API_VERSION_MINOR 0
#define SRT_V4_API_VERSION_PATCH 0
#define SRT_V4_API_VERSION                                                     \
    ((SRT_V4_API_VERSION_MAJOR << 16) | (SRT_V4_API_VERSION_MINOR << 8) |      \
     SRT_V4_API_VERSION_PATCH)

/**
 * Returns the v4 ABI version encoded as
 *   (MAJOR << 16) | (MINOR << 8) | PATCH.
 */
SRT_C_EXPORT int srt_get_v4_api_version(void);

/* ===== Error codes ===== */
//
// Mirrors 04-plugin-abi-contract.md section 4.4. Values are stable for a
// given Level; new codes may only be appended (not reordered/removed) within
// the same Level.
typedef enum {
    SRT_OK = 0,
    SRT_ERR_INVALID_ARG,
    SRT_ERR_NOT_FOUND,
    SRT_ERR_INIT_FAILED,
    SRT_ERR_NOT_INIT,
    SRT_ERR_ALREADY_INIT,
    SRT_ERR_OUT_OF_MEM,
    SRT_ERR_FILE_IO,
    SRT_ERR_UNSUPPORTED,
    SRT_ERR_TIMEOUT,
    SRT_ERR_ABORTED,
    SRT_ERR_DEPENDENCY_CYCLE,
    SRT_ERR_LEVEL_MISMATCH,
    SRT_ERR_GENERIC,
    // vnext: appended handle-table error codes (ARCH-02: append-only within Level).
    SRT_ERR_INVALID_HANDLE, ///< Handle destroyed or never created.
    SRT_ERR_MODEL_BUSY,     ///< Model is busy with another task.
} srt_error;

/* ===== Opaque handle types ===== */
//
// srt_handle is the generic opaque handle base for future v4 handle kinds
// (tensor, etc.). srt_session is the concrete session handle that owns a
// VoicebankScanner + LanguageService + Runtime composition. All handles are
// created explicitly and must be destroyed by the caller; passing NULL to any
// srt_* function is a no-op or returns SRT_ERR_INVALID_ARG.
typedef struct srt_handle_t *srt_handle;
typedef struct srt_session_t *srt_session;

/* ===== TLS error buffer ===== */

/**
 * Returns a UTF-8 string describing the last error that occurred on the
 * calling thread, or an empty string ("") if no error is pending.
 *
 * The string includes the error category, code, message, and source location
 * (when available) in the form "[Category::Code] message\n  at file:line:func".
 *
 * The returned pointer is valid until the next srt API call on the same
 * thread; the caller must NOT free it.
 */
SRT_C_EXPORT const char *srt_last_error(void);

/**
 * Returns the srt_error code of the last error that occurred on the calling
 * thread, or SRT_OK if no error is pending.
 */
SRT_C_EXPORT srt_error srt_last_error_code(void);

/**
 * Clears the per-thread last-error buffer and code.
 */
SRT_C_EXPORT void srt_clear_last_error(void);

/* ===== String ownership helpers ===== */

/**
 * Frees a UTF-8 string previously allocated and returned by an srt API
 * function. Passing NULL is a no-op.
 */
SRT_C_EXPORT void srt_free_string(char *str);

/**
 * Frees an array of UTF-8 strings previously allocated and returned by an
 * srt API function, together with the array itself.
 * Passing NULL is a no-op.
 */
SRT_C_EXPORT void srt_free_string_array(char **arr, size_t count);

/* ===== Runtime lifecycle ===== */
//
// srt_init / srt_shutdown manage process-wide runtime state (e.g. global
// service initialization). They are reference-counted: multiple srt_init
// calls require an equal number of srt_shutdown calls. Sessions created
// before srt_shutdown remain valid until explicitly destroyed.

/**
 * Initializes the synthrt runtime (process-wide).
 *
 * \return SRT_OK on success; SRT_ERR_INIT_FAILED on failure;
 *         SRT_ERR_ALREADY_INIT if already initialized (still increments the
 *         reference count and returns SRT_OK to the caller in a future
 *         implementation; currently a stub returning SRT_OK).
 */
SRT_C_EXPORT srt_error srt_init(void);

/**
 * Shuts down the synthrt runtime (process-wide).
 *
 * Calling srt_shutdown without a matching srt_init is a no-op.
 */
SRT_C_EXPORT srt_error srt_shutdown(void);

/* ===== Session lifecycle ===== */

/**
 * Creates a new synthrt session.
 *
 * The session owns a VoicebankScanner, LanguageService, and Runtime instance
 * for bank scanning, G2P route resolution, and plugin/package management. The
 * caller owns the returned handle and must release it with
 * srt_session_destroy().
 *
 * \return Non-NULL session handle on success; NULL on failure (the error is
 *         available via srt_last_error()).
 */
SRT_C_EXPORT srt_session srt_session_create(void);

/**
 * Destroys a session previously created by srt_session_create().
 *
 * Passing NULL is a no-op. The session handle becomes invalid after this
 * call; all derived resources are released.
 *
 * \return SRT_OK on success; SRT_ERR_INVALID_ARG if \p session is NULL.
 */
SRT_C_EXPORT srt_error srt_session_destroy(srt_session session);

/**
 * Sets the package search paths used by the session for bank scanning.
 *
 * Replaces any previously configured package search paths. Must be called
 * before srt_session_refresh() to take effect.
 *
 * \param session  Session handle returned by srt_session_create().
 * \param paths    Array of UTF-8 directory paths. May be NULL if \p count is 0.
 * \param count    Number of entries in \p paths.
 * \return SRT_OK on success; SRT_ERR_INVALID_ARG if \p session is NULL or
 *         \p paths is NULL while \p count > 0.
 */
SRT_C_EXPORT srt_error srt_session_set_package_paths(srt_session session,
                                                      const char *const *paths,
                                                      int count);

/**
 * Triggers bank scan and dependency resolution (Stage 1, repeatable).
 *
 * Scans the configured package search paths, parses package manifests, and
 * resolves dependencies. May be called multiple times (also after
 * initialize()).
 *
 * \param session  Session handle returned by srt_session_create().
 * \return SRT_OK on success; SRT_ERR_INVALID_ARG if \p session is NULL;
 *         SRT_ERR_NOT_INIT if package search paths have not been set;
 *         SRT_ERR_FILE_IO on filesystem or manifest parse errors.
 */
SRT_C_EXPORT srt_error srt_session_refresh(srt_session session);

/* ===== vnext: session/model/task handles ===== */
//
// The vnext C ABI surface exposes three handle kinds: session, model, task.
// Session owns the bank scanner / G2P / runtime composition; model is created
// from a singer key and runs a single stage; task represents an async request
// (refresh, G2P, S2P, or stage) and can be polled/waited/cancelled.
//
// destroy is idempotent and stable: after destroy the handle pointer still
// decodes to an invalid id and subsequent calls return SRT_ERR_INVALID_HANDLE.
// Internal cleanup is performed by the library; running tasks that hold a
// shared_ptr to the session continue until they complete.
//
// \see docs/refactoring-vnext/04-diagnostics-degradation-and-migration.md
//     section "最小 C ABI".

typedef struct srt_SessionHandle srt_SessionHandle;
typedef struct srt_ModelHandle srt_ModelHandle;
typedef struct srt_TaskHandle srt_TaskHandle;

// v3 (WP6): borrowed resource handles for srt_session_create_with_resources.
// Both are caller-owned opaque pointers; the session created from them borrows
// the underlying objects and must not outlive them.
typedef struct srt_RuntimeHandle srt_RuntimeHandle;
typedef struct srt_LanguageServiceHandle srt_LanguageServiceHandle;

/* --- Session --- */

/**
 * Creates a new vnext session handle.
 *
 * The session owns the (stub in WP7) VoicebankScanner + LanguageService +
 * Runtime composition. The caller must release the handle with
 * srt_session_destroy().
 *
 * \return Non-NULL on success; NULL on failure (see srt_last_error()).
 */
SRT_C_EXPORT srt_SessionHandle *srt_session_create_v2(void);

/**
 * Destroys a vnext session handle. Passing NULL is a no-op.
 * After destroy, the handle stably returns SRT_ERR_INVALID_HANDLE.
 */
SRT_C_EXPORT srt_error srt_session_destroy_v2(srt_SessionHandle *handle);

/**
 * Creates a session with borrowed Runtime + LanguageService. Both handles
 * must outlive the returned session. Equivalent to VoicebankSession(
 * SessionResources{...}) in C++. Passing NULL for either resource returns
 * NULL and sets last_error to InvalidArgument.
 *
 * The session borrows the underlying objects via a non-owning aliasing
 * shared_ptr; it does not extend their lifetime. The caller must keep both
 * handles valid until after the session is destroyed.
 *
 * \param runtime          Borrowed Runtime handle (non-null).
 * \param languageService  Borrowed LanguageService handle (non-null).
 * \return Non-NULL session handle on success; NULL on failure (the error is
 *         available via srt_last_error()).
 */
SRT_C_EXPORT srt_SessionHandle *srt_session_create_with_resources(
    srt_RuntimeHandle *runtime,
    srt_LanguageServiceHandle *languageService);

/* --- Runtime / LanguageService resource handles (v3 / WP6) --- */
//
// Caller-owned handles backing a Runtime / LanguageService instance. Created
// explicitly, destroyed explicitly, and borrowed by sessions created via
// srt_session_create_with_resources. destroy is idempotent: after destroy
// the handle pointer decodes to an invalid id and subsequent calls return
// SRT_ERR_INVALID_HANDLE.

/**
 * Creates a Runtime handle. The caller owns it; destroy with
 * srt_runtime_destroy.
 *
 * \return Non-NULL on success; NULL on failure (see srt_last_error()).
 */
SRT_C_EXPORT srt_RuntimeHandle *srt_runtime_create(void);

/**
 * Destroys a Runtime handle. Passing NULL is a no-op.
 * After destroy, the handle stably returns SRT_ERR_INVALID_HANDLE.
 */
SRT_C_EXPORT srt_error srt_runtime_destroy(srt_RuntimeHandle *handle);

/**
 * Creates a LanguageService handle. The caller owns it; destroy with
 * srt_language_service_destroy.
 *
 * \return Non-NULL on success; NULL on failure (see srt_last_error()).
 */
SRT_C_EXPORT srt_LanguageServiceHandle *srt_language_service_create(void);

/**
 * Destroys a LanguageService handle. Passing NULL is a no-op.
 * After destroy, the handle stably returns SRT_ERR_INVALID_HANDLE.
 */
SRT_C_EXPORT srt_error srt_language_service_destroy(srt_LanguageServiceHandle *handle);

/**
 * Sets the voicebank roots (package search paths). Takes effect on the next
 * refresh. \p roots may be NULL when \p count is 0.
 */
SRT_C_EXPORT srt_error srt_session_set_roots(srt_SessionHandle *handle,
                                              const char *const *roots,
                                              size_t count);

/**
 * Sets the reserved phoneme tokens. Takes effect on the next refresh.
 * \p phonemes may be NULL when \p count is 0.
 */
SRT_C_EXPORT srt_error srt_session_set_reserved_phonemes(
    srt_SessionHandle *handle, const char *const *phonemes, size_t count);

/**
 * Starts an asynchronous refresh. Returns a task handle the caller can
 * poll/wait/cancel, or NULL on failure (the session handle is invalid or
 * another refresh is in flight).
 */
SRT_C_EXPORT srt_TaskHandle *srt_session_refresh_async(srt_SessionHandle *handle);

/**
 * Returns a pointer to the current snapshot, or NULL if no snapshot exists.
 * The pointer is valid only until the next session mutation; callers must not
 * hold it across tasks. (Actual snapshot type is connected in WP8.)
 */
SRT_C_EXPORT const void *srt_session_snapshot(srt_SessionHandle *handle);

/* --- Model --- */

/**
 * Creates a model handle bound to \p session for the given singer key
 * (packageId, singerId, version). Returns NULL on failure.
 */
SRT_C_EXPORT srt_ModelHandle *srt_model_create(srt_SessionHandle *session,
                                                const char *packageId,
                                                const char *singerId,
                                                const char *version);

/**
 * Destroys a model handle. Passing NULL is a no-op.
 */
SRT_C_EXPORT srt_error srt_model_destroy(srt_ModelHandle *handle);

/* --- Task --- */

typedef enum {
    SRT_TASK_STATE_PENDING = 0,
    SRT_TASK_STATE_RUNNING = 1,
    SRT_TASK_STATE_SUCCEEDED = 2,
    SRT_TASK_STATE_FAILED = 3,
    SRT_TASK_STATE_CANCELLED = 4,
} srt_TaskState;

/**
 * Returns the current task state. An invalid handle returns SRT_TASK_STATE_FAILED.
 */
SRT_C_EXPORT srt_TaskState srt_task_state(srt_TaskHandle *handle);

/**
 * Waits for the task to reach a terminal state.
 *
 * \param timeout_ms Max wait in milliseconds; a negative value waits forever.
 * \return SRT_OK when the task reached a terminal state;
 *         SRT_ERR_TIMEOUT when \p timeout_ms elapsed;
 *         SRT_ERR_INVALID_HANDLE when \p handle is invalid.
 */
SRT_C_EXPORT srt_error srt_task_wait(srt_TaskHandle *handle, int timeout_ms);

/**
 * Requests cooperative cancellation. The task transitions to
 * SRT_TASK_STATE_CANCELLED only after the running work acknowledges it
 * ("取消协作完成后才终态").
 */
SRT_C_EXPORT srt_error srt_task_cancel(srt_TaskHandle *handle);

/**
 * Destroys a task handle. Passing NULL is a no-op.
 * The underlying object is released once all shared_ptr references are gone.
 */
SRT_C_EXPORT void srt_task_destroy(srt_TaskHandle *handle);

/**
 * Reads the task result as a versioned UTF-8 JSON buffer.
 *
 * \param out_size Receives the byte length (excluding NUL) on success.
 * \return Newly allocated buffer the caller must free with srt_free_buffer,
 *         or NULL when the task has not produced a result.
 */
SRT_C_EXPORT const char *srt_task_result_json(srt_TaskHandle *handle,
                                               size_t *out_size);

/**
 * Frees a buffer previously allocated by an srt API function.
 * Passing NULL is a no-op.
 */
SRT_C_EXPORT void srt_free_buffer(void *ptr);

#ifdef __cplusplus
} // extern "C"
#endif
