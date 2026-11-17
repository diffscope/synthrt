if(TARGET acoustic_objects)
add_auto_test(test_AcousticInterpreter.cpp acoustic_objects)
add_auto_test(test_AcousticInference.cpp acoustic_objects)
add_auto_test(test_AcousticTask.cpp acoustic_objects)
endif()

if(TARGET test_AcousticTask)
    set_tests_properties(test_AcousticTask PROPERTIES TIMEOUT 30)
endif()
