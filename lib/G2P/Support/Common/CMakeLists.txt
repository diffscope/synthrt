project(srt-g2p-common
        VERSION ${SRT_G2P_VERSION}
        LANGUAGES C CXX
)

find_package(cpp-pinyin CONFIG REQUIRED)
find_package(re2 CONFIG REQUIRED)

file(GLOB_RECURSE _src CONFIGURE_DEPENDS *.h *.cpp)

synthrt_add_library(${PROJECT_NAME} STATIC NO_INSTALL
        SOURCES ${_src}
        FEATURES cxx_std_20
        LINKS srt::g2p srt::core cpp-pinyin::cpp-pinyin re2::re2
        LINKS_PRIVATE $<BUILD_INTERFACE:srt-g2p-inferutil>
        INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}
)

set_target_properties(${PROJECT_NAME} PROPERTIES
        OUTPUT_NAME srt-g2p-common
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
)
