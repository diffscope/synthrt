get_filename_component(_dir_name ${CMAKE_CURRENT_LIST_DIR} NAME)
set(CURRENT_PLUGIN_CATEGORY ${_dir_name})
set(CURRENT_PLUGIN_IID org.openvpi.dsinfer.plugin.InferenceDriver)

add_subdirectory(onnxdriver)
