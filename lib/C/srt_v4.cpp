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

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <vector>

#include <diffsinger/Bank/VoicebankScanner.h>
#include <synthrt/G2P/LanguageService.h>
#include <synthrt/Core/Core/Runtime.h>

#include <synthrt/Core/Support/Error.h>
#include <synthrt/Core/Support/Expected.h>

#include "LastError.h"

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
            srt::c::detail::setLastError("srt_session_create: out of memory");
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
        srt::c::detail::setLastError("srt_session_destroy: session handle is null");
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
        srt::c::detail::setLastError("srt_session_set_package_paths: session handle is null");
        return SRT_ERR_INVALID_ARG;
    }
    if (count > 0 && !paths) {
        srt::c::detail::setLastError("srt_session_set_package_paths: paths is null");
        return SRT_ERR_INVALID_ARG;
    }

    try {
        std::vector<std::filesystem::path> pathVec;
        pathVec.reserve(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i) {
            if (!paths[i]) {
                srt::c::detail::setLastError("srt_session_set_package_paths: path entry is null");
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
        srt::c::detail::setLastError("srt_session_refresh: session handle is null");
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
