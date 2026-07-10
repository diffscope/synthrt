project(DsDict
    VERSION ${SRT_G2P_VERSION}
    LANGUAGES CXX
)

file(GLOB_RECURSE _src CONFIGURE_DEPENDS "*.h" "*.cpp")

srt_g2p_add_plugin(${PROJECT_NAME} dict ${PROJECT_NAME} NO_EXPORT
    SOURCES ${_src}
    FEATURES cxx_std_20
    LINKS srt::g2p srt::core
    INCLUDE_PRIVATE *
)
