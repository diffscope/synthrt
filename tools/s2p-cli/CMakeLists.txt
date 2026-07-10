if(NOT DEFINED SRT_S2P_VERSION)
    set(SRT_S2P_VERSION ${PROJECT_VERSION})
endif()

project(s2p-cli
    LANGUAGES CXX
    VERSION ${SRT_S2P_VERSION}
    DESCRIPTION "Syllable-to-Phoneme Command Line Tool"
)

add_executable(${PROJECT_NAME} main.cpp)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)
target_link_libraries(${PROJECT_NAME} PRIVATE srt::s2p)
target_compile_definitions(${PROJECT_NAME} PRIVATE
    TOOL_DESC="${PROJECT_DESCRIPTION}, Version ${PROJECT_VERSION}"
    TOOL_VERSION="${PROJECT_VERSION}"
)
