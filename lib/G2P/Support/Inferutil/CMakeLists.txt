project(srt-g2p-inferutil
    VERSION ${SRT_G2P_VERSION}
    LANGUAGES C CXX
)

file(GLOB_RECURSE _src CONFIGURE_DEPENDS "*.cpp" "*.h")

synthrt_add_library(${PROJECT_NAME} STATIC NO_INSTALL
    SOURCES ${_src}
    FEATURES cxx_std_20
    LINKS srt::g2p re2::re2
    INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/include
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME srt-g2p-inferutil
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)
