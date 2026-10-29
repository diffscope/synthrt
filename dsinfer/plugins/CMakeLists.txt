find_package(stdcorelib-plugin CONFIG REQUIRED)

function(dsinfer_add_plugin_metadata target iid)
    stdc_add_plugin_metadata(TARGET ${target} IID ${iid})

    # The file name of a plugin must match the name in its plugin.json, and the bundle loader
    # searches only for that name with the platform prefix and suffix. The debug postfix of the
    # library targets would rename onnxdriver.dll to onnxdriverd.dll and make every plugin
    # undiscoverable in a Debug build on Windows, so plugin targets clear the postfix.
    set_target_properties(${target} PROPERTIES DEBUG_POSTFIX "")

    if(DSINFER_INSTALL)
        qm_add_copy_command(${target}
            SOURCES plugin.json
            INSTALL_DIR .
        )
    else()
        qm_add_copy_command(${target}
            SKIP_INSTALL
            SOURCES plugin.json
        )
    endif()
endfunction()

add_subdirectory(singerproviders)

add_subdirectory(inferencedrivers)

add_subdirectory(inferenceinterpreters)
