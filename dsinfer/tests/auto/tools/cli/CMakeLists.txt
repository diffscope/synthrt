set(_cli_source "${SYNTHRT_SOURCE_DIR}/dsinfer/tools/cli")
foreach(_part IN ITEMS SynthesisInput CliRuntime SynthesisRunner)
    add_auto_test(test_${_part}.cpp synthrt-dsinfer inputparser wavfile)
    if(TARGET test_${_part})
        target_sources(test_${_part} PRIVATE "${_cli_source}/${_part}.cpp")
        target_include_directories(test_${_part} PRIVATE "${_cli_source}")
    endif()
endforeach()

add_auto_test(test_SynthesisRunner_Pipeline.cpp synthrt-dsinfer inputparser wavfile)
if(TARGET test_SynthesisRunner_Pipeline)
    target_sources(test_SynthesisRunner_Pipeline PRIVATE "${_cli_source}/SynthesisRunner.cpp")
    target_include_directories(test_SynthesisRunner_Pipeline PRIVATE "${_cli_source}")
    add_dependencies(test_SynthesisRunner_Pipeline duration pitch variance acoustic vocoder diffsinger)
    target_compile_definitions(test_SynthesisRunner_Pipeline PRIVATE
        DSINFER_TEST_INFERENCE_PLUGINS="$<TARGET_FILE_DIR:acoustic>/.."
        DSINFER_TEST_SINGER_PLUGINS="$<TARGET_FILE_DIR:diffsinger>/.."
    )
endif()
