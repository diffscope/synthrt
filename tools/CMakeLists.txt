include(GNUInstallDirs)

file(RELATIVE_PATH SYNTHRT_TOOLS_BINDIR_TO_LIBDIR
    "${CMAKE_INSTALL_FULL_BINDIR}"
    "${CMAKE_INSTALL_FULL_LIBDIR}"
)
file(RELATIVE_PATH SYNTHRT_TOOLS_BINDIR_TO_DATADIR
    "${CMAKE_INSTALL_FULL_BINDIR}"
    "${CMAKE_INSTALL_FULL_DATADIR}"
)

set(_synthrt_tools_generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/RuntimeLayout.h.in"
    "${_synthrt_tools_generated_dir}/RuntimeLayout.h"
    @ONLY
)

if(SYNTHRT_INSTALL AND SYNTHRT_INSTALL_TOOLS AND UNIX)
    if(APPLE)
        set(SYNTHRT_TOOLS_INSTALL_RPATH "@loader_path/${SYNTHRT_TOOLS_BINDIR_TO_LIBDIR}")
    else()
        set(SYNTHRT_TOOLS_INSTALL_RPATH "$ORIGIN/${SYNTHRT_TOOLS_BINDIR_TO_LIBDIR}")
    endif()
endif()

add_subdirectory(dspk-pack-cli)
add_subdirectory(dsinfer-cli)
add_subdirectory(s2p-cli)

target_include_directories(dspk-pack-cli PRIVATE "${_synthrt_tools_generated_dir}")
target_include_directories(dsinfer-cli PRIVATE "${_synthrt_tools_generated_dir}")
target_include_directories(s2p-cli PRIVATE "${_synthrt_tools_generated_dir}")
