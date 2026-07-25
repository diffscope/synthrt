if (NOT DEFINED DSINFER_VERSION)
    set(DSINFER_VERSION ${PROJECT_VERSION})
endif ()

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
        srt-ds::infer srt::g2p srt::s2p syscmdline::syscmdline bit7z::bit7z64 srt::audio
)

# TD-CLI-10: DSINFER_CLI_HAS_OPENDSPX 定义条件明确化。
# 当且仅当 vcpkg 提供 opendspx::opendspxserializer 目标时（find_package QUIET
# 在第 16 行），定义此宏。两种构建配置（定义 / 未定义）的运行时行为：
#   - 已定义：parseDspx() 被调用，DSPX 文件正常解析
#   - 未定义：main.cpp 输出 "DSPX parsing is not enabled (DSINFER_CLI_HAS_OPENDSPX
#             not defined)" 并返回 -1（exitCode 255 on Windows）
# 测试覆盖：unittests/tools/tst_dsinfer_cli.cpp 中校验 dspx 模式不会崩溃。
if (TARGET opendspx::opendspxserializer)
    target_link_libraries(${PROJECT_NAME} PRIVATE opendspx::opendspxserializer)
    target_compile_definitions(${PROJECT_NAME} PRIVATE DSINFER_CLI_HAS_OPENDSPX)
endif ()

target_compile_definitions(${PROJECT_NAME} PRIVATE
        TOOL_DESC="${PROJECT_DESCRIPTION}, Version ${PROJECT_VERSION}"
        TOOL_VERSION="${PROJECT_VERSION}"
)

if (SYNTHRT_INSTALL AND SYNTHRT_INSTALL_TOOLS)
    if (UNIX)
        set_target_properties(${PROJECT_NAME} PROPERTIES
                INSTALL_RPATH "${SYNTHRT_TOOLS_INSTALL_RPATH}"
        )
    endif ()
    install(TARGETS ${PROJECT_NAME}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif ()

if (SYNTHRT_INSTALL AND SYNTHRT_INSTALL_TOOLS AND SYNTHRT_INSTALL_DEPLOY)
    # Install 7zip runtime
    if (WIN32)
        set(_so_ext ".dll")
        set(_so_dest ${CMAKE_INSTALL_BINDIR})
    else ()
        if (APPLE)
            set(_so_ext ".dylib")
        else ()
            set(_so_ext ".so*")
        endif ()

        set(_so_dest ${CMAKE_INSTALL_LIBDIR})
    endif ()

    if (CMAKE_CONFIGURATION_TYPES)
        set(_deploy_configs ${CMAKE_CONFIGURATION_TYPES})
    elseif (CMAKE_BUILD_TYPE)
        set(_deploy_configs ${CMAKE_BUILD_TYPE})
    else ()
        set(_deploy_configs Release)
    endif ()

    foreach (_config IN LISTS _deploy_configs)
        string(TOUPPER "${_config}" _config_upper)
        foreach (_deps_path IN LISTS QMSETUP_APPLOCAL_DEPS_PATHS_${_config_upper})
            get_filename_component(_searching_path "${_deps_path}"
                    ABSOLUTE BASE_DIR ${CMAKE_SOURCE_DIR}
            )
            file(GLOB _seven_zip_lib "${_searching_path}/*7zip*${_so_ext}")
            install(FILES ${_seven_zip_lib}
                    DESTINATION ${_so_dest}
                    CONFIGURATIONS ${_config}
            )
        endforeach ()
    endforeach ()
endif ()

set(_runtime_plugin_targets
        duration pitch variance acoustic vocoder diffsinger
        MandarinG2p CantoneseG2p Multig2p ChainG2p DsDict
)
if (TARGET srt-onnxdriver)
    list(APPEND _runtime_plugin_targets srt-onnxdriver)
endif ()
add_dependencies(${PROJECT_NAME} ${_runtime_plugin_targets})

set(_runtime_g2p_packages
        Phonetic-Suite-Cmn
        Phonetic-Suite-Jpn
        Phonetic-Suite-Num
        Phonetic-Suite-Punc
        Phonetic-Suite-Unknown
        Phonetic-Suite-Yue
)
add_custom_target(${PROJECT_NAME}-runtime-resources
        COMMAND ${CMAKE_COMMAND} -E remove_directory
        "${CMAKE_BINARY_DIR}/share/synthrt/G2pPackages"
        COMMENT "Staging dsinfer-cli runtime resources"
        VERBATIM
)
foreach (_package IN LISTS _runtime_g2p_packages)
    add_custom_command(TARGET ${PROJECT_NAME}-runtime-resources POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${SYNTHRT_SOURCE_DIR}/resources/G2pPackages/${_package}"
            "${CMAKE_BINARY_DIR}/share/synthrt/G2pPackages/${_package}"
            VERBATIM
    )
endforeach ()
add_dependencies(${PROJECT_NAME} ${PROJECT_NAME}-runtime-resources)

get_property(_is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if (_is_multi_config)
    set(_runtime_plugin_layout
            "duration|diffsinger/inferenceinterpreters/duration"
            "pitch|diffsinger/inferenceinterpreters/pitch"
            "variance|diffsinger/inferenceinterpreters/variance"
            "acoustic|diffsinger/inferenceinterpreters/acoustic"
            "vocoder|diffsinger/inferenceinterpreters/vocoder"
            "diffsinger|diffsinger/singerproviders/diffsinger"
            "MandarinG2p|srt-g2p/G2ps/MandarinG2p"
            "CantoneseG2p|srt-g2p/G2ps/CantoneseG2p"
            "LstmG2p|srt-g2p/G2ps/LstmG2p"
            "ChainG2p|srt-g2p/G2ps/ChainG2p"
            "DsDict|srt-g2p/dict/DsDict"
            "srt-onnxdriver|srt-driver/inferencedrivers/srt-onnxdriver"
    )
    foreach (_entry IN LISTS _runtime_plugin_layout)
        string(REPLACE "|" ";" _parts "${_entry}")
        list(GET _parts 0 _target)
        list(GET _parts 1 _relative_dir)
        if (TARGET ${_target})
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "$<TARGET_FILE_DIR:${_target}>"
                    "${CMAKE_BINARY_DIR}/lib/$<CONFIG>/plugins/${_relative_dir}"
                    VERBATIM
            )
        endif ()
    endforeach ()
endif ()
