# This test exercises the ONNX driver end to end and includes OnnxTensor.h, so it is defined only
# if the onnxdriver target exists. Without the ONNX Runtime package the test is omitted instead of
# failing to compile.
if(TARGET onnxdriver)
    add_auto_test(test_InferenceDriverFactory.cpp dsinfer
        RESOURCES
            resources/cases/mixed_type_ops.json
            resources/models/mixed_type_ops.onnx
    )
endif()
add_auto_test(test_InferenceCompatibility.cpp dsinfer)

if(TARGET test_InferenceDriverFactory)
    target_sources(test_InferenceDriverFactory PRIVATE TestCaseLoader.cpp)
endif()

if(TARGET test_InferenceDriverFactory AND TARGET onnxdriver)
    add_dependencies(test_InferenceDriverFactory onnxdriver)
    target_link_libraries(test_InferenceDriverFactory PRIVATE onnxutil)
    target_compile_definitions(test_InferenceDriverFactory PRIVATE ORT_API_MANUAL_INIT)
    # The test selects the ONNX Runtime that the driver loads, as a host application does in any
    # other deployment. The plugin deploys no ONNX Runtime beside itself, so the test takes the
    # payload directory from ONNXRUNTIME_BUILDS_RUNTIME_DIR, which the package defines.
    find_package(onnxruntime-builds CONFIG QUIET)
    target_compile_definitions(test_InferenceDriverFactory PRIVATE
        DSINFER_TEST_DRIVER_PLUGIN_PATH="$<TARGET_FILE_DIR:onnxdriver>/.."
        DSINFER_TEST_ORT_RUNTIME_DIR="${ONNXRUNTIME_BUILDS_RUNTIME_DIR}"
    )
endif()

if(TARGET test_InferenceCompatibility AND TARGET acoustic AND TARGET pitch AND TARGET vocoder AND
   TARGET diffsinger)
    add_dependencies(test_InferenceCompatibility acoustic pitch vocoder diffsinger)
    target_compile_definitions(test_InferenceCompatibility PRIVATE
        DSINFER_TEST_INFERENCE_PLUGIN_PATH="$<TARGET_FILE_DIR:acoustic>/.."
        DSINFER_TEST_SINGER_PLUGIN_PATH="$<TARGET_FILE_DIR:diffsinger>/.."
    )
endif()
