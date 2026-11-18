project(dsinfer-installer
    LANGUAGES CXX
    VERSION ${DSINFER_VERSION}
    DESCRIPTION "DiffSinger Manual Package Installer"
)

find_package(stdcorelib CONFIG REQUIRED)

dsinfer_add_executable(${PROJECT_NAME} TEST CONSOLE
    SOURCES main.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE stdcorelib::stdcorelib)
