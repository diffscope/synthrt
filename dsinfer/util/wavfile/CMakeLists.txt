project(wavfile)

file(GLOB_RECURSE _src "src/*.cpp")

dsinfer_add_library(${PROJECT_NAME} STATIC NO_INSTALL
    SOURCES
        ${_src}
    FEATURES
        cxx_std_17
    INCLUDE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    INCLUDE_PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/include
)