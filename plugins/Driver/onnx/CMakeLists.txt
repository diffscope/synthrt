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

# Normalize to forward slashes for portability. On Windows CI the workspace
# path may contain backslashes (e.g. "D:\a\synthrt") which break CMake string
# parsing when the path is embedded in a list argument ("\a" is an invalid
# escape). file(TO_CMAKE_PATH) converts native separators to "/".
file(TO_CMAKE_PATH "${_onnxruntime_dir}" _onnxruntime_dir)

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
    FEATURES cxx_std_20
    LINKS srt::driver srt::core
    LINKS_PRIVATE stduuid BLAKE3::blake3
    INCLUDE_PRIVATE * ${_onnxruntime_default_dir}/include
    DEFINES ORT_API_MANUAL_INIT ${_onnxdriver_ep_macros}
)

# Resolve ONNX Runtime shared libraries at configure time.
# We use file(GLOB ...) instead of bare glob pattern strings so that
# install(FILES ...) receives actual file paths — install(FILES ...) does
# not expand glob patterns itself.
if(WIN32)
    file(GLOB _ort_default_lib_files ${_onnxruntime_default_dir}/lib/*.dll)
elseif(APPLE)
    file(GLOB _ort_default_lib_files ${_onnxruntime_default_dir}/lib/*.dylib)
else()
    file(GLOB _ort_default_lib_files ${_onnxruntime_default_dir}/lib/*.so
         ${_onnxruntime_default_dir}/lib/*.so.*)
endif()

# Build-phase copy: place runtimes next to the plugin output directory.
# SKIP_INSTALL disables the install logic in qm_add_copy_command, whose
# _rel_path computation breaks when vcpkg overrides
# CMAKE_LIBRARY_OUTPUT_DIRECTORY (output goes to ${CMAKE_BINARY_DIR}/lib
# instead of ${QMSETUP_BUILD_DIR}/lib, making _rel_path start with "../"
# and resolve to the wrong install directory).
#
# Install-phase copy: use install(FILES ...) with a relative DESTINATION.
# Relative DESTINATION is resolved against the install-time
# CMAKE_INSTALL_PREFIX, which is correct for vcpkg multi-config installs
# where Debug and Release use different install prefixes.
if(_ort_default_lib_files)
    qm_add_copy_command(${PROJECT_NAME} SKIP_INSTALL
        SOURCES ${_ort_default_lib_files}
        DESTINATION runtimes/onnx/default
    )
    install(FILES ${_ort_default_lib_files}
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/plugins/srt-driver/inferencedrivers/${PROJECT_NAME}/runtimes/onnx/default
    )
endif()

if(EXISTS ${_onnxruntime_cuda_dir} AND IS_DIRECTORY ${_onnxruntime_cuda_dir})
    if(WIN32)
        file(GLOB _ort_cuda_lib_files ${_onnxruntime_cuda_dir}/lib/*.dll)
    else()
        file(GLOB _ort_cuda_lib_files ${_onnxruntime_cuda_dir}/lib/*.so
             ${_onnxruntime_cuda_dir}/lib/*.so.*)
    endif()

    if(NOT APPLE AND _ort_cuda_lib_files)
        qm_add_copy_command(${PROJECT_NAME} SKIP_INSTALL
            SOURCES ${_ort_cuda_lib_files}
            DESTINATION runtimes/onnx/cuda
        )
        install(FILES ${_ort_cuda_lib_files}
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/plugins/srt-driver/inferencedrivers/${PROJECT_NAME}/runtimes/onnx/cuda
        )
    endif()
endif()
