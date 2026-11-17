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

# The driver as a host reaches it: through the drivers directory and the backend its manifest
# names
add_auto_test(test_InferenceDriverFactory_Plugin.cpp synthrt-dsinfer)
if(TARGET test_InferenceDriverFactory_Plugin)
    add_dependencies(test_InferenceDriverFactory_Plugin onnxdriver)
    target_compile_definitions(test_InferenceDriverFactory_Plugin PRIVATE
        DSINFER_TEST_DRIVER_PLUGIN_PATH="$<TARGET_FILE_DIR:onnxdriver>/.."
    )
endif()
