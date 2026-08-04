include(GNUInstallDirs)

# ----------------------------------
# Project Constants
# ----------------------------------
set(DSINFER_INCLUDE_DIR "include")

# Install the CMake package files and the public headers alongside the binaries.
# The generic helpers gate both on <proj>_DEVEL, which defaults to off.
set(DSINFER_DEVEL ON)

# Keep the plugin layout: <lib>/plugins/<name>/<category>.
set(DSINFER_BUILD_PLUGINS_DIR ${QMSETUP_BUILD_DIR}/lib/plugins/${DSINFER_INSTALL_NAME})
set(DSINFER_INSTALL_PLUGINS_DIR ${CMAKE_INSTALL_LIBDIR}/plugins/${DSINFER_INSTALL_NAME})

# Windows resource metadata.
set(DSINFER_RC_DESCRIPTION "${PROJECT_DESCRIPTION}")
set(DSINFER_RC_COPYRIGHT "Copyright (c) 2023-present Team OpenVPI")

function(_dsinfer_common_configure_target _target)
    if(WIN32)
        qm_add_win_rc(${_target}
            NAME ${DSINFER_INSTALL_NAME}
            DESCRIPTION "${DSINFER_RC_DESCRIPTION}"
            COPYRIGHT "${DSINFER_RC_COPYRIGHT}"
        )
    endif()

    dsinfer_set_default_install_rpath(${_target})
endfunction()

set(DSINFER_POST_CONFIGURE_COMMANDS _dsinfer_common_configure_target)

# ----------------------------------
# Include Build Helpers
# ----------------------------------
qm_import(private/BuildSystem)
qm_setup_build_repo_helpers()
