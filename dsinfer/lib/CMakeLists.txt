file(GLOB_RECURSE _src "*.cpp")

if(SYNTHRT_BUILD_STATIC)
    set(_type STATIC)
else()
    set(_type SHARED)
endif()

add_library(${PROJECT_NAME} ${_type} ${_src})

target_compile_definitions(${PROJECT_NAME} PRIVATE DSINFER_LIBRARY)

target_link_libraries(${PROJECT_NAME} PRIVATE synthrt sparsepp)

target_include_directories(${PROJECT_NAME} PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../include>)

target_include_directories(${PROJECT_NAME} PRIVATE
    ../include/dsinfer
    ../include/dsinfer/Inference
    ../include/dsinfer/Support
)