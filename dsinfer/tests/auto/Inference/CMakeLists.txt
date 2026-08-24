add_auto_test(test_InferenceDriverFactory.cpp dsinfer)

if(TARGET test_InferenceDriverFactory AND TARGET onnxdriver)
    add_dependencies(test_InferenceDriverFactory onnxdriver)
    target_link_libraries(test_InferenceDriverFactory PRIVATE onnxutil)
    target_compile_definitions(test_InferenceDriverFactory PRIVATE ORT_API_MANUAL_INIT)
    target_compile_definitions(test_InferenceDriverFactory PRIVATE
        DSINFER_TEST_DRIVER_PLUGIN_PATH="$<TARGET_FILE_DIR:onnxdriver>/.."
        DSINFER_TEST_ONNX_MODEL_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../../manual/onnxdriver/resources/models/mixed_type_ops.onnx"
    )
endif()
