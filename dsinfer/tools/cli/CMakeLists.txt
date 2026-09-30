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

# The ONNX driver is the only plugin here that may be absent. It is not built if the
# onnxruntime-builds package is not installed, which is a supported configuration, so it is listed
# only if the target exists. An unconditional dependency on a missing target fails at generate
# time.
set(_dsinfer_cli_plugins diffsinger duration pitch variance acoustic vocoder)

if(TARGET onnxdriver)
    list(APPEND _dsinfer_cli_plugins onnxdriver)
endif()

add_dependencies(${PROJECT_NAME} ${_dsinfer_cli_plugins})
