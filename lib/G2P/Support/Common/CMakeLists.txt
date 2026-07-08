project(PinyinG2pCommon
        VERSION ${SRT_G2P_VERSION}
        LANGUAGES C CXX
)

find_package(cpp-pinyin CONFIG REQUIRED)
find_package(re2 CONFIG REQUIRED)

file(GLOB_RECURSE _src CONFIGURE_DEPENDS *.h *.cpp)

srt_g2p_add_library(${PROJECT_NAME} STATIC NO_INSTALL
        SOURCES ${_src}
        FEATURES cxx_std_17
        LINKS srt::g2p srt::core cpp-pinyin::cpp-pinyin re2::re2
        LINKS_PRIVATE $<BUILD_INTERFACE:InferUtil>
        INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}
)
