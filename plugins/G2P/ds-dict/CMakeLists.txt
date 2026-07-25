# DsDict - G2P 字典查询插件。
#
# 注册 IID "srt.g2p.TaskPlugin"，key "dict.dsdict"。

if(NOT TARGET srt::g2p)
    message(WARNING "DsDict will not be built: srt::g2p target not found.")
    return()
endif()

project(DsDict
    VERSION ${SRT_G2P_VERSION}
    LANGUAGES CXX
)

file(GLOB_RECURSE _src CONFIGURE_DEPENDS "*.h" "*.cpp")

synthrt_add_plugin(${PROJECT_NAME} dict ${PROJECT_NAME} NO_EXPORT
    SOURCES ${_src}
    FEATURES cxx_std_20
    LINKS srt::g2p srt::core
    INCLUDE_PRIVATE *
)
