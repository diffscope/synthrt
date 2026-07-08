project(LstmG2p
        VERSION ${SRT_G2P_VERSION}
        LANGUAGES C CXX
)

find_package(re2 CONFIG REQUIRED)

file(GLOB_RECURSE _src CONFIGURE_DEPENDS *.h *.cpp)

srt_g2p_add_plugin(${PROJECT_NAME} G2ps ${PROJECT_NAME} NO_EXPORT
        SOURCES ${_src}
        FEATURES cxx_std_17
        LINKS srt::g2p srt::core re2::re2
        LINKS_PRIVATE $<BUILD_INTERFACE:InferUtil>
        INCLUDE_PRIVATE *
        DEFINES
)
