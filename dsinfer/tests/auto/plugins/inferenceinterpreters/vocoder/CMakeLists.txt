if(TARGET vocoder_objects)
add_auto_test(test_VocoderInterpreter.cpp vocoder_objects)
add_auto_test(test_VocoderInference.cpp vocoder_objects)
add_auto_test(test_VocoderTask.cpp vocoder_objects)
endif()

if(TARGET test_VocoderTask)
    set_tests_properties(test_VocoderTask PROPERTIES TIMEOUT 30)
endif()
