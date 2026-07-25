project(srt-c
    LANGUAGES CXX
    VERSION 0.0.1.0
    DESCRIPTION "synthrt v4 C API FFI layer"
)

synthrt_declare_target(srt_c "${PROJECT_NAME}" ${PROJECT_VERSION} "Team OpenVPI" 2023 "../../include")

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
synthrt_add_library(${PROJECT_NAME} STATIC
    SOURCES ${_src}
    LINKS stdcorelib::stdcorelib srt::core srt-ds::bank srt::g2p srt::driver srt-ds::infer srt-ds::session
    INCLUDE_PRIVATE ../../include/synthrt/C/** **
)

# Convenience alias matching the v3 roadmap convention (srt::c).
if(NOT TARGET srt::c)
    add_library(srt::c ALIAS ${PROJECT_NAME})
endif()

synthrt_install()
