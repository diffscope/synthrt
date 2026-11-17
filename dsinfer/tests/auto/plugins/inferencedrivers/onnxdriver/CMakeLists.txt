if(NOT TARGET onnxdriver)
    return()
endif()

# The ONNX Runtime payload that the driver loads, as the driver factory test takes it
find_package(onnxruntime-builds CONFIG QUIET)

# The model image cache of the driver, through the objects of the plugin. A loaded driver shows
# that a session runs, but not whether two sessions share one image.
add_auto_test(test_Session_Cache.cpp onnxdriver_objects
    RESOURCES
        ../../../Inference/resources/models/mixed_type_ops.onnx
        ../../../Inference/resources/models/duration_word_encoder.onnx
)
if(TARGET test_Session_Cache)
    target_compile_definitions(test_Session_Cache PRIVATE
        DSINFER_TEST_ORT_RUNTIME_DIR="${ONNXRUNTIME_BUILDS_RUNTIME_DIR}"
    )
endif()

add_auto_test(test_ScopedTimer.cpp onnxdriver_objects)
add_auto_test(test_SessionSystem.cpp onnxdriver_objects)
