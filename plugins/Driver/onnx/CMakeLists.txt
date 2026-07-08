# OnnxDriver plugin - ONNX Runtime inference driver for srt-driver.
#
# This plugin migrates the dsinfer onnxdriver to the srt-driver module.
# OnnxTensor/onnxutil now live inside the srt-driver library itself, so the
# plugin only needs to link srt::driver (which exposes OnnxTensor).

# Require the srt-driver library (carries OnnxTensor).
if(NOT TARGET srt::driver)
    message(WARNING "OnnxDriver plugin will not be built: srt::driver target not found.")
    return()
endif()

# Require onnxruntime headers.
set(_onnxruntime_dir ${SYNTHRT_SOURCE_DIR}/third-party/onnxruntime)
set(_onnxruntime_default_dir ${_onnxruntime_dir}/default)

if(NOT IS_DIRECTORY ${_onnxruntime_default_dir})
    message(WARNING "OnnxDriver plugin will not be built: ONNX runtime not found at ${_onnxruntime_default_dir}.")
    return()
endif()

project(srt-onnxdriver
    VERSION ${SRT_DRIVER_VERSION}
    LANGUAGES C CXX
)

file(GLOB_RECURSE _src *.h *.cpp)

set(_onnxruntime_cuda_dir ${_onnxruntime_dir}/cuda)

if(WIN32 AND SRT_DRIVER_ENABLE_DIRECTML)
    set(_onnxdriver_ep_macro_dml "ONNXDRIVER_ENABLE_DML")
endif()

if(SRT_DRIVER_ENABLE_CUDA)
    set(_onnxdriver_ep_macro_cuda "ONNXDRIVER_ENABLE_CUDA")
endif()

set(_onnxdriver_ep_macros ${_onnxdriver_ep_macro_dml} ${_onnxdriver_ep_macro_cuda})

find_package(stduuid CONFIG REQUIRED)
find_package(blake3 CONFIG REQUIRED)

# synthrt is built in-tree and provides srt::LogCategory / srt::Logger used by
# the plugin logger. The target is globally visible from the top-level CMake.

srt_driver_add_plugin(${PROJECT_NAME} inferencedrivers ${PROJECT_NAME} NO_EXPORT
    SOURCES ${_src}
    FEATURES cxx_std_17
    LINKS srt::driver srt::core
    LINKS_PRIVATE stduuid BLAKE3::blake3
    INCLUDE_PRIVATE * ${_onnxruntime_default_dir}/include
    DEFINES ORT_API_MANUAL_INIT ${_onnxdriver_ep_macros}
)

if(WIN32)
    set(_ort_default_lib_files ${_onnxruntime_default_dir}/lib/*.dll)
elseif(APPLE)
    set(_ort_default_lib_files ${_onnxruntime_default_dir}/lib/*.dylib)
else()
    set(_ort_default_lib_files ${_onnxruntime_default_dir}/lib/*.so ${_onnxruntime_default_dir}/lib/*.so.*)
endif()

# Copy onnxruntime shared libraries next to the plugin output.
qm_add_copy_command(${PROJECT_NAME} # SKIP_INSTALL
    SOURCES ${_ort_default_lib_files}
    DESTINATION runtimes/onnx/default
    INSTALL_DIR ${CMAKE_INSTALL_PREFIX}
)

if(EXISTS ${_onnxruntime_cuda_dir} AND IS_DIRECTORY ${_onnxruntime_cuda_dir})
    if(WIN32)
        set(_ort_cuda_lib_files ${_onnxruntime_cuda_dir}/lib/*.dll)
    else()
        set(_ort_cuda_lib_files ${_onnxruntime_cuda_dir}/lib/*.so ${_onnxruntime_cuda_dir}/lib/*.so.*)
    endif()

    if(NOT APPLE)
        qm_add_copy_command(${PROJECT_NAME} # SKIP_INSTALL
            SOURCES ${_ort_cuda_lib_files}
            DESTINATION runtimes/onnx/cuda
            INSTALL_DIR ${CMAKE_INSTALL_PREFIX}
        )
    endif()
endif()
