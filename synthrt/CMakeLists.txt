project(synthrt
    LANGUAGES CXX
    VERSION 0.0.1.0
    DESCRIPTION "Synthesizer Runtime Library"
)

# ----------------------------------
# Project Variables
# ----------------------------------
set(SYNTHRT_AUTHOR "Team OpenVPI")
set(SYNTHRT_START_YEAR 2023)
set(SYNTHRT_INCLUDE_DIR include)

include("${SYNTHRT_SOURCE_DIR}/cmake/BuildAPI.cmake")

add_subdirectory(lib)

if(SYNTHRT_BUILD_TESTS)
    add_subdirectory(tests)
endif()

synthrt_install()

if(SYNTHRT_INSTALL)
    # Install copyright
    install(FILES ../LICENSE
        DESTINATION ${CMAKE_INSTALL_DATADIR}/${SYNTHRT_INSTALL_NAME}
    )

    if(WIN32)
        set(_lib_dir bin)
    else()
        set(_lib_dir lib)
    endif()

    if(WIN32)
        set(_extra_search_path ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
    else()
        set(_extra_search_path ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})
    endif()

    qm_import(Deploy)
    qm_deploy_directory(${CMAKE_INSTALL_PREFIX}
        LIBRARY_DIR ${_lib_dir}
        EXTRA_SEARCHING_PATHS ${_extra_search_path}
        VERBOSE
    )

    # Install vcruntime
    if(MSVC AND SYNTHRT_INSTALL_MSVC_RUNTIME)
        set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION "${OUTPUT_DIR}")
        include(InstallRequiredSystemLibraries)
    endif()
endif()