project(inputparser)

file(GLOB_RECURSE _src "*.cpp")

dsinfer_add_library(${PROJECT_NAME} STATIC NO_INSTALL
        SOURCES ${_src}
        LINKS dsinfer
        INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/include
)