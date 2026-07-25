# MandarinG2p - G2P 普通话拼音插件。
#
# 注册 IID "srt.g2p.TaskPlugin"，key "g2p.template.MandarinG2pInference"。

if(NOT TARGET srt::g2p)
    message(WARNING "MandarinG2p will not be built: srt::g2p target not found.")
    return()
endif()

project(MandarinG2p
        VERSION ${SRT_G2P_VERSION}
        LANGUAGES CXX
)

find_package(re2 CONFIG REQUIRED)
find_package(cpp-pinyin CONFIG REQUIRED)

file(GLOB_RECURSE _src CONFIGURE_DEPENDS *.h *.cpp)

synthrt_add_plugin(${PROJECT_NAME} G2ps ${PROJECT_NAME} NO_EXPORT
        SOURCES ${_src}
        FEATURES cxx_std_20
        LINKS srt::g2p srt::core cpp-pinyin::cpp-pinyin re2::re2 srt-g2p-common
        LINKS_PRIVATE $<BUILD_INTERFACE:srt-g2p-inferutil>
        INCLUDE_PRIVATE *
        DEFINES
)
