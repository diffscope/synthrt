if(TARGET variance_objects)
add_auto_test(test_VarianceInterpreter.cpp variance_objects)
add_auto_test(test_VarianceInference.cpp variance_objects)
add_auto_test(test_VarianceTask.cpp variance_objects)
endif()

if(TARGET test_VarianceTask)
    set_tests_properties(test_VarianceTask PROPERTIES TIMEOUT 30)
endif()
