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
# ONNX Runtime is provided by the onnxruntime-builds CMake package (vcpkg
# overlay port derived from its own scripts/setup-onnxruntime.cmake); find it
# here so consumers never pass paths from the port. Without the package the
# plugin degrades to not being built (same behavior as before the split).
find_package(onnxruntime-builds CONFIG QUIET)
if(NOT TARGET onnxruntime-builds::default)
    message(WARNING "OnnxDriver plugin will not be built: ONNX Runtime package (onnxruntime-builds) not found.")
    return()
endif()

project(srt-onnxdriver
    VERSION ${SRT_DRIVER_VERSION}
    LANGUAGES C CXX
)

file(GLOB_RECURSE _src CONFIGURE_DEPENDS *.h *.cpp)

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

synthrt_add_plugin(${PROJECT_NAME} inferencedrivers ${PROJECT_NAME} NO_EXPORT
    SOURCES ${_src}
    FEATURES cxx_std_20
    LINKS srt::driver srt::core
    LINKS_PRIVATE stduuid BLAKE3::blake3
    INCLUDE_PRIVATE * ${ONNXRUNTIME_BUILDS_INCLUDE_DIR}
    DEFINES ORT_API_MANUAL_INIT ${_onnxdriver_ep_macros}
)

# Resolve ONNX Runtime shared libraries at configure time.
# We use file(GLOB ...) instead of bare glob pattern strings so that
# install(FILES ...) receives actual file paths — install(FILES ...) does
# not expand glob patterns itself.
if(WIN32)
    file(GLOB _ort_default_lib_files ${ONNXRUNTIME_BUILDS_RUNTIME_DIR}/*.dll)
elseif(APPLE)
    file(GLOB _ort_default_lib_files ${ONNXRUNTIME_BUILDS_RUNTIME_DIR}/*.dylib)
else()
    file(GLOB _ort_default_lib_files ${ONNXRUNTIME_BUILDS_RUNTIME_DIR}/*.so
         ${ONNXRUNTIME_BUILDS_RUNTIME_DIR}/*.so.*)
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

if(ONNXRUNTIME_BUILDS_CUDA_RUNTIME_DIR)
    if(WIN32)
        file(GLOB _ort_cuda_lib_files ${ONNXRUNTIME_BUILDS_CUDA_RUNTIME_DIR}/*.dll)
    else()
        file(GLOB _ort_cuda_lib_files ${ONNXRUNTIME_BUILDS_CUDA_RUNTIME_DIR}/*.so
             ${ONNXRUNTIME_BUILDS_CUDA_RUNTIME_DIR}/*.so.*)
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
