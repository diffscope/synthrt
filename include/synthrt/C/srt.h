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
 * The returned pointer is valid until the next srt API call on the same
 * thread; the caller must NOT free it.
 */
SRT_C_EXPORT const char *srt_last_error(void);

/**
 * Clears the per-thread last-error buffer.
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
 * Sets the plugin search paths used by the session for plugin discovery.
 *
 * Replaces any previously configured plugin search paths.
 *
 * \param session  Session handle returned by srt_session_create().
 * \param paths    Array of UTF-8 directory paths. May be NULL if \p count is 0.
 * \param count    Number of entries in \p paths.
 * \return SRT_OK on success; SRT_ERR_INVALID_ARG if \p session is NULL or
 *         \p paths is NULL while \p count > 0.
 */
SRT_C_EXPORT srt_error srt_session_set_plugin_paths(srt_session session,
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

#ifdef __cplusplus
} // extern "C"
#endif
