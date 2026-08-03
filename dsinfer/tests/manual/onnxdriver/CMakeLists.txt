cmake_minimum_required(VERSION 3.15)
project(tst_onnxdriver LANGUAGES CXX)

# Check if the 'onnxdriver' target exists (adjust this to your actual dependency if needed)
if(NOT TARGET onnxdriver)
    message(WARNING "target onnxdriver not built, skipping test target.")
    return()
endif()

set(Boost_USE_STATIC_LIBS OFF)
find_package(Boost 1.67.0 QUIET COMPONENTS unit_test_framework CONFIG)

if(NOT Boost_FOUND)
    message(WARNING "Boost not found, auto tests will not be built")
    return()
endif()

file(GLOB_RECURSE _src *.cpp *.h)

add_executable(${PROJECT_NAME} ${_src})

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)

target_link_libraries(${PROJECT_NAME} PRIVATE Boost::unit_test_framework dsinfer)

target_compile_definitions(${PROJECT_NAME} PRIVATE BOOST_TEST_DYN_LINK)

# resources directory name in current source dir
set(_src_res_dir "resources")

# resources directory name in build dir
set(_res_dir "${PROJECT_NAME}_resources")

# Copy resources to the build directory after building
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND
            ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/${_src_res_dir}
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/${_res_dir}
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
        RESOURCE_DIR=\"${_res_dir}\"
)

add_dependencies(${PROJECT_NAME} onnxdriver)
