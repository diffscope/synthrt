if(NOT TARGET onnxutil)
    message(WARNING "OnnxDriver plugin will not be built.")
    return()
endif()

project(onnxdriver
    VERSION ${DSINFER_VERSION}
    LANGUAGES C CXX
)

file(GLOB_RECURSE _src *.h *.cpp)

set(_onnxruntime_dir ${SYNTHRT_SOURCE_DIR}/third-party/onnxruntime)
set(_onnxruntime_default_dir ${_onnxruntime_dir}/default)
set(_onnxruntime_cuda_dir ${_onnxruntime_dir}/cuda)

if(WIN32 AND DSINFER_ENABLE_DIRECTML)
    set(_onnxdriver_ep_macro_dml "ONNXDRIVER_ENABLE_DML")
endif()

if(DSINFER_ENABLE_CUDA)
    set(_onnxdriver_ep_macro_cuda "ONNXDRIVER_ENABLE_CUDA")
endif()

set(_onnxdriver_ep_macros ${_onnxdriver_ep_macro_dml} ${_onnxdriver_ep_macro_cuda})

find_package(stduuid CONFIG REQUIRED)
find_package(blake3 CONFIG REQUIRED)

dsinfer_add_plugin(${PROJECT_NAME} ${CURRENT_PLUGIN_CATEGORY} NO_EXPORT
    SOURCES ${_src}
    FEATURES cxx_std_17
    LINKS dsinfer
    LINKS_PRIVATE stduuid BLAKE3::blake3 $<BUILD_INTERFACE:onnxutil>
    INCLUDE_PRIVATE *
    DEFINES ORT_API_MANUAL_INIT ${_onnxdriver_ep_macros}
)

if(WIN32)
    set(_ort_default_lib_files ${_onnxruntime_default_dir}/lib/*.dll)
elseif(APPLE)
    set(_ort_default_lib_files ${_onnxruntime_default_dir}/lib/*.dylib)
else()
    set(_ort_default_lib_files ${_onnxruntime_default_dir}/lib/*.so ${_onnxruntime_default_dir}/lib/*.so.*)
endif()

# Copy onnxruntime shared libraries
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
