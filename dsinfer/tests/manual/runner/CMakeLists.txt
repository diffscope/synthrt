project(dsinfer-runner
    LANGUAGES CXX
    VERSION ${DSINFER_VERSION}
    DESCRIPTION "DiffSinger Manual Inference Runner"
)

dsinfer_add_executable(${PROJECT_NAME} TEST CONSOLE
    SOURCES
        RunnerRuntime.cpp
        RunnerRuntime.h
        SynthesisInput.cpp
        SynthesisInput.h
        SynthesisRunner.cpp
        SynthesisRunner.h
        main.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    synthrt-dsinfer
    inputparser
    wavfile
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    DSINFER_RUNNER_VERSION="${PROJECT_VERSION}"
)

find_package(onnxruntime-builds CONFIG QUIET)
if(TARGET onnxruntime-builds::default)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        DSINFER_RUNNER_ORT_RUNTIME_DIR="${ONNXRUNTIME_BUILDS_RUNTIME_DIR}"
    )
endif()

# The plugins this runner discovers are not link dependencies. They must still be built before a
# manual run, because the runner finds them in the build's plugin tree.
set(_dsinfer_runner_plugins diffsinger duration pitch variance acoustic vocoder)
if(TARGET onnxdriver)
    list(APPEND _dsinfer_runner_plugins onnxdriver)
endif()
add_dependencies(${PROJECT_NAME} ${_dsinfer_runner_plugins})
