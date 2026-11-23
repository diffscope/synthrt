include("conf.cmake")

synthrt_init_buildsystem()

add_subdirectory(lib)

if(SYNTHRT_BUILD_TESTS)
    add_subdirectory(tests)
endif()

synthrt_install()

synthrt_finish_buildsystem()

if(SYNTHRT_INSTALL)
    # Install copyright
    install(FILES ../LICENSE
        DESTINATION ${CMAKE_INSTALL_DATADIR}/${SYNTHRT_INSTALL_NAME}
    )

    # Install vcruntime
    if(MSVC AND SYNTHRT_INSTALL_MSVC_RUNTIME)
        set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION "")
        include(InstallRequiredSystemLibraries)
    endif()

    # Install cmake modules
    set(_install_dir ${CMAKE_INSTALL_LIBDIR}/cmake/${SYNTHRT_INSTALL_NAME})
    install(DIRECTORY ../cmake/
        DESTINATION ${_install_dir}/cmake
    )
endif()