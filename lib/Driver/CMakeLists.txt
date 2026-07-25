project(srt-driver
    LANGUAGES CXX
    VERSION 0.0.1.0
    DESCRIPTION "synthrt v3 inference driver abstraction"
)

synthrt_declare_target(srt_driver "${PROJECT_NAME}" ${PROJECT_VERSION} "Team OpenVPI" 2023 "../../include")

option(SRT_DRIVER_ENABLE_DIRECTML "Enable DirectML provider for the ONNX driver plugin" ON)
option(SRT_DRIVER_ENABLE_CUDA "Enable CUDA provider for the ONNX driver plugin" ON)

file(GLOB_RECURSE _src CONFIGURE_DEPENDS "*.cpp")

find_package(stdcorelib CONFIG REQUIRED)

# Import onnxruntime headers for OnnxTensor (zero-copy Ort::Value backend).
# With ORT_API_MANUAL_INIT the C++ wrapper is header-only; no onnxruntime
# library link is required (the shared library is loaded at runtime by the
# OnnxDriver plugin).
# Locate ONNX Runtime: check SYNTHRT_ORT_DIR (CI), then third-party/ (local), then build dir
if(DEFINED SYNTHRT_ORT_DIR AND IS_DIRECTORY "${SYNTHRT_ORT_DIR}/default")
    set(_onnxruntime_dir ${SYNTHRT_ORT_DIR})
elseif(IS_DIRECTORY ${SYNTHRT_SOURCE_DIR}/third-party/onnxruntime/default)
    set(_onnxruntime_dir ${SYNTHRT_SOURCE_DIR}/third-party/onnxruntime)
elseif(IS_DIRECTORY ${CMAKE_BINARY_DIR}/onnxruntime/default)
    set(_onnxruntime_dir ${CMAKE_BINARY_DIR}/onnxruntime)
else()
    set(_onnxruntime_dir ${SYNTHRT_SOURCE_DIR}/third-party/onnxruntime)
endif()
set(_onnxruntime_default_dir ${_onnxruntime_dir}/default)

set(_onnx_include_dirs "")
set(_onnx_defines "")
if(IS_DIRECTORY ${_onnxruntime_default_dir})
    set(_onnx_include_dirs ${_onnxruntime_default_dir}/include)
    set(_onnx_defines ORT_API_MANUAL_INIT)
else()
    message(WARNING "ONNX runtime not found at ${_onnxruntime_default_dir}. OnnxTensor sources will be excluded.")

    # Filter out onnx-specific sources when onnxruntime is unavailable so the
    # srt-driver library can still build without ONNX support.
    list(FILTER _src EXCLUDE REGEX "/onnx/[^/]*\\.cpp$")
endif()

# srt-core is built in-tree; the srt::core alias target is available directly.
synthrt_add_library(${PROJECT_NAME} SHARED
    SOURCES ${_src}
    LINKS stdcorelib::stdcorelib srt::core
    INCLUDE_PRIVATE ../../include/synthrt/Driver/** **
    DEFINES ${_onnx_defines}
)

# OnnxTensor.h is a public header that includes <onnxruntime_cxx_api.h>, so the
# ONNX Runtime include directory must be PUBLIC to propagate to consumers
# (e.g. dsinfer) that include synthrt/Driver/onnx/OnnxTensor.h.
if(_onnx_include_dirs)
    target_include_directories(${PROJECT_NAME} PUBLIC
        $<BUILD_INTERFACE:${_onnx_include_dirs}>
    )
endif()

# Convenience alias matching the v3 roadmap convention (srt::driver).
if(NOT TARGET srt::driver)
    add_library(srt::driver ALIAS ${PROJECT_NAME})
endif()

synthrt_install()
