add_auto_test(test_InferenceDriverFactory.cpp dsinfer
    RESOURCES
        resources/cases/mixed_type_ops.json
        resources/models/mixed_type_ops.onnx
)
add_auto_test(test_InferenceCompatibility.cpp dsinfer)

if(TARGET test_InferenceDriverFactory)
    target_sources(test_InferenceDriverFactory PRIVATE TestCaseLoader.cpp)
endif()

if(TARGET test_InferenceDriverFactory AND TARGET onnxdriver)
    add_dependencies(test_InferenceDriverFactory onnxdriver)
    target_link_libraries(test_InferenceDriverFactory PRIVATE onnxutil)
    target_compile_definitions(test_InferenceDriverFactory PRIVATE ORT_API_MANUAL_INIT)
    target_compile_definitions(test_InferenceDriverFactory PRIVATE
        DSINFER_TEST_DRIVER_PLUGIN_PATH="$<TARGET_FILE_DIR:onnxdriver>/.."
    )
endif()

if(TARGET test_InferenceCompatibility AND TARGET acoustic AND TARGET vocoder AND TARGET diffsinger)
    add_dependencies(test_InferenceCompatibility acoustic vocoder diffsinger)
    target_compile_definitions(test_InferenceCompatibility PRIVATE
        DSINFER_TEST_INFERENCE_PLUGIN_PATH="$<TARGET_FILE_DIR:acoustic>/.."
        DSINFER_TEST_SINGER_PLUGIN_PATH="$<TARGET_FILE_DIR:diffsinger>/.."
    )
endif()
