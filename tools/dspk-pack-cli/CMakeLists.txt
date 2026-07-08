if(NOT DEFINED DSBANK_VERSION)
    set(DSBANK_VERSION ${PROJECT_VERSION})
endif()

project(dspk-pack-cli
    LANGUAGES CXX
    VERSION ${DSBANK_VERSION}
    DESCRIPTION "DiffSinger Package Command Line Tool"
)

file(GLOB_RECURSE _src CONFIGURE_DEPENDS "*.cpp")

add_executable(${PROJECT_NAME} ${_src})
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)
target_link_libraries(${PROJECT_NAME} PRIVATE srt-ds::bank)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    TOOL_DESC="${PROJECT_DESCRIPTION}, Version ${PROJECT_VERSION}"
    TOOL_VERSION="${PROJECT_VERSION}"
)
