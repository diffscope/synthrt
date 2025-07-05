get_filename_component(_dir_name ${CMAKE_CURRENT_LIST_DIR} NAME)
set(CURRENT_PLUGIN_CATEGORY ${_dir_name})

add_subdirectory(duration)

add_subdirectory(pitch)

add_subdirectory(variance)

add_subdirectory(acoustic)

add_subdirectory(vocoder)