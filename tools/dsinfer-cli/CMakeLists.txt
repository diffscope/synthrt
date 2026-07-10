if(NOT DEFINED DSINFER_VERSION)
    set(DSINFER_VERSION ${PROJECT_VERSION})
endif()

project(dsinfer-cli
    LANGUAGES CXX
    VERSION ${DSINFER_VERSION}
    DESCRIPTION "DiffSinger Inference Command Line Tool"
)

file(GLOB_RECURSE _src CONFIGURE_DEPENDS "*.cpp")

find_package(syscmdline CONFIG REQUIRED)
find_package(bit7z CONFIG REQUIRED)
find_package(nlohmann_json CONFIG QUIET)
find_package(opendspx CONFIG QUIET)

add_executable(${PROJECT_NAME} ${_src})
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/include/diffsinger/Infer)
target_link_libraries(${PROJECT_NAME} PRIVATE
    srt-ds::infer srt::g2p srt::s2p syscmdline::syscmdline bit7z::bit7z64 wavfile
)

if(TARGET opendspx::opendspxserializer)
    target_link_libraries(${PROJECT_NAME} PRIVATE opendspx::opendspxserializer)
    target_compile_definitions(${PROJECT_NAME} PRIVATE DSINFER_CLI_HAS_OPENDSPX)
endif()

# Load G2P pinyin dictionary from the bundled Phonetic-Suite-Cmn test package
set(DSINFER_CLI_G2P_DICT_DIR "${CMAKE_SOURCE_DIR}/tests/common/G2pPackages/Phonetic-Suite-Cmn/modules/G2p-Cmn/dict"
    CACHE PATH "G2P Mandarin dictionary directory")
if(EXISTS "${DSINFER_CLI_G2P_DICT_DIR}/mandarin/word.txt")
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        DSINFER_CLI_G2P_DICT_DIR="${DSINFER_CLI_G2P_DICT_DIR}"
    )
endif()

# G2P packages directory for the G2P Manager
set(DSINFER_CLI_G2P_PACKAGES_DIR "${CMAKE_SOURCE_DIR}/tests/common/G2pPackages"
    CACHE PATH "G2P packages root directory")
if(EXISTS "${DSINFER_CLI_G2P_PACKAGES_DIR}")
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        DSINFER_CLI_G2P_PACKAGES_DIR="${DSINFER_CLI_G2P_PACKAGES_DIR}"
    )
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE
    TOOL_DESC="${PROJECT_DESCRIPTION}, Version ${PROJECT_VERSION}"
    TOOL_VERSION="${PROJECT_VERSION}"
)

if(DEFINED DSINFER_INSTALL AND DSINFER_INSTALL AND SYNTHRT_INSTALL_DEPLOY)
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

add_dependencies(${PROJECT_NAME} duration pitch variance acoustic vocoder)
