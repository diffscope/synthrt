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
# OnnxDriver plugin). ONNX Runtime is provided by the onnxruntime-builds CMake
# package (vcpkg overlay port); find_package locates headers with no port-side
# path. Without the package, OnnxTensor sources are excluded as before.
find_package(onnxruntime-builds CONFIG QUIET)
set(_onnx_include_dirs "")
set(_onnx_defines "")
if(TARGET onnxruntime-builds::default)
    set(_onnx_include_dirs "${ONNXRUNTIME_BUILDS_INCLUDE_DIR}")
    set(_onnx_defines ORT_API_MANUAL_INIT)
else()
    message(WARNING "ONNX Runtime package (onnxruntime-builds) not found. OnnxTensor sources will be excluded.")

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
