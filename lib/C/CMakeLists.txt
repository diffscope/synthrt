project(srt-c
    LANGUAGES CXX
    VERSION 0.0.1.0
    DESCRIPTION "synthrt v4 C API FFI layer"
)

set(srt-c_VAR_PREFIX srt_c)
set(SRT_C_INSTALL ${SYNTHRT_INSTALL})
set(SRT_C_VERSION ${PROJECT_VERSION})
set(SRT_C_INSTALL_NAME ${PROJECT_NAME})
set(SRT_C_AUTHOR "Team OpenVPI")
set(SRT_C_START_YEAR 2023)
set(SRT_C_INCLUDE_DIR ../../include)
include("${SYNTHRT_SOURCE_DIR}/cmake/BuildAPI.cmake")

# srt-c library (static FFI layer)
#
# Static library so that downstream consumers (C# P/Invoke, ctypes, native
# executables) link the FFI implementation directly without an extra DLL
# dependency. The C++ implementation modules (srt-core, srt-ds-bank, srt-g2p,
# srt-driver, srt-ds-infer, srt-ds-session) are pulled in as transitive link
# requirements.
#
# WP8: srt_v4.cpp now delegates the vnext session/model/task C ABI to the real
# ds::session::VoicebankSession + ModelSetHandle pipeline. srt-ds::session is
# linked directly (not just transitively) because HandleTable.h includes
# <diffsinger/Session/VoicebankSession.h> and <diffsinger/Session/ModelSetHandle.h>.
# The TLS error buffer (LastError.cpp) and string ownership helpers are shared
# v4 utilities.

file(GLOB_RECURSE _src CONFIGURE_DEPENDS "*.cpp")

find_package(stdcorelib CONFIG REQUIRED)

# P5.7: added DS Infer link for the Interpreter/Inference C API (srt_infer_*).
# WP8: added srt-ds::session for VoicebankSession / ModelSetHandle delegation.
srt_c_add_library(${PROJECT_NAME} STATIC
    SOURCES ${_src}
    LINKS stdcorelib::stdcorelib srt::core srt-ds::bank srt::g2p srt::driver srt-ds::infer srt-ds::session
    INCLUDE_PRIVATE ../../include/synthrt/C/** **
)

# Convenience alias matching the v3 roadmap convention (srt::c).
if(NOT TARGET srt::c)
    add_library(srt::c ALIAS ${PROJECT_NAME})
endif()
if(NOT TARGET synthrt::c)
    add_library(synthrt::c ALIAS ${PROJECT_NAME})
endif()

srt_c_install()
