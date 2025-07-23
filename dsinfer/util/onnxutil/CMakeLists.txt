# Import onnxruntime
set(_onnxruntime_dir ${SYNTHRT_SOURCE_DIR}/third-party/onnxruntime)
set(_onnxruntime_default_dir ${_onnxruntime_dir}/default)

if(NOT IS_DIRECTORY ${_onnxruntime_default_dir})
    message(WARNING "ONNX runtime not found. Please download and build it from https://github.com/microsoft/onnxruntime.")
    return()
endif()

project(onnxutil)

file(GLOB_RECURSE _src "*.cpp")

dsinfer_add_library(${PROJECT_NAME} STATIC NO_INSTALL
    SOURCES ${_src}
    LINKS dsinfer
    INCLUDE ${CMAKE_CURRENT_SOURCE_DIR} ${_onnxruntime_default_dir}/include
    DEFINES ORT_API_MANUAL_INIT
)