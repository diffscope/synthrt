find_package(stdcorelib-plugin CONFIG REQUIRED)

function(dsinfer_add_plugin_metadata target iid)
    stdc_add_plugin_metadata(TARGET ${target} IID ${iid})

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
