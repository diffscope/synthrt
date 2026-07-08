project(inputparser)

file(GLOB_RECURSE _src "*.cpp")

dsinfer_add_library(${PROJECT_NAME} STATIC NO_INSTALL
        SOURCES ${_src}
        LINKS srt-ds::infer
        INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_SOURCE_DIR}/include/diffsinger/Infer)
