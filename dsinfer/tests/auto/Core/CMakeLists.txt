add_auto_test(test_Tensor.cpp synthrt-dsinfer)
add_auto_test(test_ParamTag.cpp synthrt-dsinfer)
set(_entry_plugins acoustic duration pitch variance vocoder diffsinger)
add_auto_test(test_ContribInterpreterPlugin_Dsinfer.cpp synthrt-dsinfer)
if(TARGET test_ContribInterpreterPlugin_Dsinfer)
    add_dependencies(test_ContribInterpreterPlugin_Dsinfer ${_entry_plugins})
    foreach(_plugin IN LISTS _entry_plugins)
        string(TOUPPER "${_plugin}" _upper)
        target_compile_definitions(test_ContribInterpreterPlugin_Dsinfer PRIVATE
            DSINFER_TEST_${_upper}_FILE="$<TARGET_FILE:${_plugin}>"
            DSINFER_TEST_${_upper}_MANIFEST="$<TARGET_PROPERTY:${_plugin},SOURCE_DIR>/plugin.json"
        )
    endforeach()
endif()
