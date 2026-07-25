if(NOT DEFINED SRT_S2P_VERSION)
    set(SRT_S2P_VERSION ${PROJECT_VERSION})
endif()

project(s2p-cli
    LANGUAGES CXX
    VERSION ${SRT_S2P_VERSION}
    DESCRIPTION "Syllable-to-Phoneme Command Line Tool"
)

add_executable(${PROJECT_NAME} main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/json_escape.cpp
)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)
target_link_libraries(${PROJECT_NAME} PRIVATE srt::s2p)
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
