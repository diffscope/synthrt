project(dsinfer-cli
    LANGUAGES CXX
    VERSION ${DSINFER_VERSION}
    DESCRIPTION "DiffSinger Inference Command Line Tool"
)

file(GLOB_RECURSE _src "*.cpp")

add_executable(${PROJECT_NAME} ${_src})
target_link_libraries(${PROJECT_NAME} PRIVATE
    dsinfer
    inputparser
    wavfile
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    TOOL_DESC="${PROJECT_DESCRIPTION}, Version ${PROJECT_VERSION}"
    TOOL_VERSION="${PROJECT_VERSION}"
)

add_dependencies(${PROJECT_NAME} onnxdriver duration pitch variance acoustic vocoder)
