project(dsinfer-cli
    LANGUAGES CXX
    VERSION ${DSINFER_VERSION}
    DESCRIPTION "DiffSinger Inference Command Line Tool"
)

file(GLOB_RECURSE _src "*.cpp")

find_package(syscmdline CONFIG REQUIRED)
find_package(unofficial-bit7z CONFIG REQUIRED)

dsinfer_add_executable(${PROJECT_NAME}
    SOURCES ${_src}
    LINKS_PRIVATE dsinfer syscmdline::syscmdline unofficial::bit7z::bit7z64
        $<BUILD_INTERFACE:inputparser>
        $<BUILD_INTERFACE:wavfile>
    RC_NAME ${PROJECT_NAME}
    RC_DESCRIPTION ${PROJECT_DESCRIPTION}
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    TOOL_DESC="${PROJECT_DESCRIPTION}, Version ${PROJECT_VERSION}"
    TOOL_VERSION="${PROJECT_VERSION}"
)

if(DSINFER_INSTALL AND SYNTHRT_INSTALL_DEPLOY)
    # Install 7zip runtime
    if(WIN32)
        set(_so_ext ".dll")
        set(_so_dest ${CMAKE_INSTALL_BINDIR})
    else()
        if(APPLE)
            set(_so_ext ".dylib")
        else()
            set(_so_ext ".so*")
        endif()

        set(_so_dest ${CMAKE_INSTALL_LIBDIR})
    endif()

    string(TOUPPER ${CMAKE_BUILD_TYPE} _build_type_upper)
    get_filename_component(_searching_path
        "${QMSETUP_APPLOCAL_DEPS_PATHS_${_build_type_upper}}"
        ABSOLUTE BASE_DIR ${CMAKE_SOURCE_DIR}
    )

    file(GLOB _seven_zip_lib "${_searching_path}/*7zip*${_so_ext}")
    install(FILES ${_seven_zip_lib}
        DESTINATION ${_so_dest}
    )
endif()

add_dependencies(${PROJECT_NAME} onnxdriver duration pitch variance acoustic vocoder)