if(NOT TARGET onnxutil)
    message(WARNING "OnnxDriver plugin will not be built.")
    return()
endif()

project(onnxdriver
    VERSION ${DSINFER_VERSION}
    LANGUAGES C CXX
)

file(GLOB_RECURSE _src *.h *.cpp)

if(WIN32 AND DSINFER_ENABLE_DIRECTML)
    set(_onnxdriver_ep_macro_dml "ONNXDRIVER_ENABLE_DML")
endif()

if(DSINFER_ENABLE_CUDA)
    set(_onnxdriver_ep_macro_cuda "ONNXDRIVER_ENABLE_CUDA")
endif()

set(_onnxdriver_ep_macros ${_onnxdriver_ep_macro_dml} ${_onnxdriver_ep_macro_cuda})

find_package(stduuid CONFIG REQUIRED)
find_package(blake3 CONFIG REQUIRED)

dsinfer_add_plugin(${PROJECT_NAME} CATEGORY ${CURRENT_PLUGIN_CATEGORY}/onnx NO_EXPORT
    SOURCES ${_src}
    FEATURES cxx_std_17
    LINKS dsinfer
    LINKS_PRIVATE stduuid BLAKE3::blake3 $<BUILD_INTERFACE:onnxutil>
    INCLUDE_PRIVATE *
    DEFINES ORT_API_MANUAL_INIT ${_onnxdriver_ep_macros}
)

dsinfer_add_plugin_metadata(${PROJECT_NAME} ${CURRENT_PLUGIN_IID})

# This plugin neither deploys ONNX Runtime nor refers to an ONNX Runtime path.
#
# The driver does not search for ONNX Runtime. It loads ONNX Runtime from the directory that the
# host passes in DriverInitArgs::runtimePath, and an empty path falls back to the search of the
# platform loader. Deployment is therefore the responsibility of the application, which is the
# editor if synthrt is embedded and the test executable if synthrt is built alone.
#
# The ONNX Runtime headers used by this plugin are provided by onnxutil, which links the
# onnxruntime-builds imported target publicly.
