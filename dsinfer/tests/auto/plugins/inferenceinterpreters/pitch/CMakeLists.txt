if(TARGET pitch_objects)
add_auto_test(test_PitchInterpreter.cpp pitch_objects)
add_auto_test(test_PitchInference.cpp pitch_objects)
add_auto_test(test_PitchTask.cpp pitch_objects)
endif()

if(TARGET test_PitchTask)
    set_tests_properties(test_PitchTask PROPERTIES TIMEOUT 30)
endif()
