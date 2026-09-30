# ONNX Runtime is provided by the onnxruntime-builds package, a vcpkg overlay port, which alone
# determines the ONNX Runtime version. This file specifies neither a version nor a path. The
# imported target carries the include directory, so a different ONNX Runtime payload requires no
# change here.
#
# Without the package, neither onnxutil nor the ONNX driver plugin is built.
find_package(onnxruntime-builds CONFIG QUIET)

if(NOT TARGET onnxruntime-builds::default)
    message(WARNING "ONNX Runtime package (onnxruntime-builds) not found, so the ONNX driver "
                    "plugin will not be built. The vcpkg \"onnx\" feature installs the package.")
    return()
endif()

project(onnxutil)

file(GLOB_RECURSE _src "*.cpp")

# LINKS is public, so the ONNX driver plugin obtains the ONNX Runtime include directory through
# this target and requires no ONNX Runtime path of its own.
dsinfer_add_library(${PROJECT_NAME} STATIC NO_INSTALL
    SOURCES ${_src}
    LINKS dsinfer onnxruntime-builds::default
    INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}
    DEFINES ORT_API_MANUAL_INIT
)