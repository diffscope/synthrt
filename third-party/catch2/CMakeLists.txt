add_library(Catch2 STATIC
    src/catch_amalgamated.cpp
)

target_include_directories(Catch2 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/catch2
)

target_compile_features(Catch2 PUBLIC cxx_std_14)

add_library(Catch2::Catch2 ALIAS Catch2)

add_library(Catch2WithMain STATIC
    src/catch_main.cpp
)

target_link_libraries(Catch2WithMain PUBLIC Catch2)

add_library(Catch2::Catch2WithMain ALIAS Catch2WithMain)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/extras")
set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH}" PARENT_SCOPE)
