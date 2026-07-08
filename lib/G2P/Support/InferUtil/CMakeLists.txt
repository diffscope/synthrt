project(InferUtil
    VERSION ${SRT_G2P_VERSION}
    LANGUAGES C CXX
)

file(GLOB_RECURSE _src CONFIGURE_DEPENDS "*.cpp" "*.h")

srt_g2p_add_library(${PROJECT_NAME} STATIC NO_INSTALL
    SOURCES ${_src}
    FEATURES cxx_std_17
    LINKS srt::g2p re2::re2
    INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/include
)

set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME srt-g2p-InferUtil)
