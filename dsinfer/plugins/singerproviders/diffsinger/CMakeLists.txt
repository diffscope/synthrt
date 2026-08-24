project(diffsinger
    VERSION ${DSINFER_VERSION}
    LANGUAGES CXX
)

file(GLOB_RECURSE _src *.h *.cpp)

dsinfer_add_plugin(${PROJECT_NAME} CATEGORY ${CURRENT_PLUGIN_CATEGORY}/diffsinger NO_EXPORT
    SOURCES ${_src}
    FEATURES cxx_std_17
    LINKS dsinfer
    LINKS_PRIVATE
    INCLUDE_PRIVATE *
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/plugin.json
        $<TARGET_FILE_DIR:${PROJECT_NAME}>/plugin.json
)

if(DSINFER_INSTALL)
    install(FILES plugin.json
        DESTINATION ${DSINFER_INSTALL_PLUGINS_DIR}/${CURRENT_PLUGIN_CATEGORY}/diffsinger
    )
endif()
