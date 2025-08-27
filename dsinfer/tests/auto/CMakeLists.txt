project(tst_dsinfer_auto)

set(Boost_USE_STATIC_LIBS OFF)
find_package(Boost 1.67.0 QUIET COMPONENTS unit_test_framework CONFIG)

if(NOT Boost_FOUND)
    message(WARNING "Boost not found, auto tests will not be built")
    return()
endif()

file(GLOB_RECURSE _src *.cpp)
add_executable(${PROJECT_NAME} ${_src})

target_link_libraries(${PROJECT_NAME} PRIVATE Boost::unit_test_framework)
target_link_libraries(${PROJECT_NAME} PRIVATE dsinfer)