if(NOT DEFINED DSBANK_VERSION)
    set(DSBANK_VERSION ${PROJECT_VERSION})
endif()

project(dspk-pack-cli
    LANGUAGES CXX
    VERSION ${DSBANK_VERSION}
    DESCRIPTION "DiffSinger Package Command Line Tool"
)

file(GLOB_RECURSE _src CONFIGURE_DEPENDS "*.cpp")

# TD-CLI-06: 链接 tools/common 共享源（escapeJsonString）
list(APPEND _src "${CMAKE_CURRENT_SOURCE_DIR}/../common/json_escape.cpp")

add_executable(${PROJECT_NAME} ${_src})
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)
target_link_libraries(${PROJECT_NAME} PRIVATE srt-ds::bank)
target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../common")

target_compile_definitions(${PROJECT_NAME} PRIVATE
    TOOL_DESC="${PROJECT_DESCRIPTION}, Version ${PROJECT_VERSION}"
    TOOL_VERSION="${PROJECT_VERSION}"
)

if(SYNTHRT_INSTALL AND SYNTHRT_INSTALL_TOOLS)
    if(UNIX)
        set_target_properties(${PROJECT_NAME} PROPERTIES
            INSTALL_RPATH "${SYNTHRT_TOOLS_INSTALL_RPATH}"
        )
    endif()
    install(TARGETS ${PROJECT_NAME}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()
