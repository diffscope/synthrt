# ----------------------------------
# Project Constants
# ----------------------------------
set(SYNTHRT_INCLUDE_DIR "include")

# Install the CMake package files and the public headers alongside the binaries.
# The generic helpers gate both on <proj>_DEVEL, which defaults to off.
set(SYNTHRT_DEVEL ON)

# Windows resource metadata.
set(SYNTHRT_RC_DESCRIPTION "${PROJECT_DESCRIPTION}")
set(SYNTHRT_RC_COPYRIGHT "Copyright (c) 2023-present Team OpenVPI")

function(_synthrt_common_configure_target _target)
    if(WIN32)
        qm_add_win_rc(${_target}
            NAME ${SYNTHRT_INSTALL_NAME}
            DESCRIPTION "${SYNTHRT_RC_DESCRIPTION}"
            COPYRIGHT "${SYNTHRT_RC_COPYRIGHT}"
        )
    endif()

    synthrt_set_default_install_rpath(${_target})
endfunction()

set(SYNTHRT_POST_CONFIGURE_COMMANDS _synthrt_common_configure_target)

# ----------------------------------
# Include Build Helpers
# ----------------------------------
set(QM_BUILD_REPO_HELPERS_FUNCTION_PREFIX synthrt)
include(${SYNTHRT_SOURCE_DIR}/cmake/QMBuildRepoHelpers.cmake)
