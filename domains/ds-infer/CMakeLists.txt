project(dsinfer
    LANGUAGES CXX
    VERSION 0.0.1.3
    DESCRIPTION "DiffSinger Inference Framework"
)

# ----------------------------------
# Build Options
# ----------------------------------
option(DSINFER_ENABLE_DIRECTML "Enable DirectML provider" ON)
option(DSINFER_ENABLE_CUDA "Enable CUDA provider" ON)
option(DSINFER_ENABLE_STATIC_PLUGINS "Enable static plugin linking" OFF) # TODO: Implement this option

# ----------------------------------
# Configure BuildAPI
# ----------------------------------
synthrt_declare_target(dsinfer "${PROJECT_NAME}" ${PROJECT_VERSION} "Team OpenVPI" 2023 "../../include")

# ----------------------------------
# Add source modules
# ----------------------------------
set(CMAKE_FOLDER "Domains")
add_subdirectory(lib)

if(SYNTHRT_BUILD_TESTS)
    set(CMAKE_FOLDER "Tests")
    add_subdirectory(unittests)
endif()

synthrt_install()
