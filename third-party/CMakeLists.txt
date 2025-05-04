# stdcorelib
set(STDCORELIB_INSTALL OFF)
add_subdirectory(stdcorelib)

# sparsepp
add_library(sparsepp INTERFACE)
target_include_directories(
    sparsepp SYSTEM INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sparsepp>
)