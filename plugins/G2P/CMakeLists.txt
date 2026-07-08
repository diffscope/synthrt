project(srt-g2p-plugins
    VERSION ${PROJECT_VERSION}
    LANGUAGES CXX
)

set(srt-g2p-plugins_VAR_PREFIX srt_g2p)
set(srt-g2p_VAR_PREFIX srt_g2p)
set(SRT_G2P_INSTALL ${SYNTHRT_INSTALL})
set(SRT_G2P_VERSION ${PROJECT_VERSION})
set(SRT_G2P_INSTALL_NAME srt-g2p)
set(SRT_G2P_AUTHOR "Team OpenVPI")
set(SRT_G2P_START_YEAR 2023)
set(SRT_G2P_INCLUDE_DIR ../../include)
include("${SYNTHRT_SOURCE_DIR}/cmake/BuildAPI.cmake")

add_subdirectory(mandarin)
add_subdirectory(cantonese)
add_subdirectory(lstm)
add_subdirectory(chain)
add_subdirectory(ds-dict)
