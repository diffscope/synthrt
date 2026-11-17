if(NOT TARGET diffsinger_objects OR NOT TARGET acoustic OR NOT TARGET pitch OR NOT TARGET vocoder)
    return()
endif()
add_auto_test(test_DiffSingerProvider_Compatibility.cpp synthrt-dsinfer)
add_auto_test(test_DiffSingerProvider.cpp diffsinger_objects)
add_auto_test(test_DiffSingerPipelineExecutive.cpp diffsinger_objects)
foreach(_test IN ITEMS test_DiffSingerProvider_Compatibility test_DiffSingerProvider test_DiffSingerPipelineExecutive)
    if(TARGET ${_test})
        add_dependencies(${_test} acoustic pitch vocoder diffsinger)
        target_compile_definitions(${_test} PRIVATE
            DSINFER_TEST_INFERENCE_PLUGIN_PATH="$<TARGET_FILE_DIR:acoustic>/.."
            DSINFER_TEST_SINGER_PLUGIN_PATH="$<TARGET_FILE_DIR:diffsinger>/.."
        )
    endif()
endforeach()
