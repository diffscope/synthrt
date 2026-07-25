# Multig2p - G2P 多语言模型推理插件。
#
# 注册 IID "srt.g2p.TaskPlugin"，key "g2p.model.Multig2pInference"。

if(NOT TARGET srt::g2p)
    message(WARNING "Multig2p will not be built: srt::g2p target not found.")
    return()
endif()

project(Multig2p
        VERSION ${SRT_G2P_VERSION}
        LANGUAGES CXX
)

find_package(re2 CONFIG REQUIRED)

file(GLOB_RECURSE _src CONFIGURE_DEPENDS *.h *.cpp)

synthrt_add_plugin(${PROJECT_NAME} G2ps ${PROJECT_NAME} NO_EXPORT
        SOURCES ${_src}
        FEATURES cxx_std_20
        LINKS srt::g2p srt::core re2::re2
        LINKS_PRIVATE $<BUILD_INTERFACE:srt-g2p-inferutil>
        INCLUDE_PRIVATE *
        DEFINES
)
