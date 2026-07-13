cmake_minimum_required(VERSION 3.19)

set(_expected_packages
    dsinfer
    srt-audio
    srt-c
    srt-core
    srt-driver
    srt-ds-bank
    srt-extract
    srt-g2p
    srt-s2p
    srt-svs
    synthrt
)
set(_g2p_packages
    Phonetic-Suite-Cmn
    Phonetic-Suite-Jpn
    Phonetic-Suite-Num
    Phonetic-Suite-Punc
    Phonetic-Suite-Unknown
    Phonetic-Suite-Yue
)

if(DEFINED SELF_TEST_ROOT AND NOT SELF_TEST_ROOT STREQUAL "")
    get_filename_component(_self_test_root "${SELF_TEST_ROOT}" ABSOLUTE)
    set(_prefix "${_self_test_root}/prefix")
    set(_libdir custom-lib)
    set(_datadir custom-share)
    file(REMOVE_RECURSE "${_self_test_root}")
    file(MAKE_DIRECTORY "${_prefix}/${_libdir}/cmake/extra-sibling")
    set(_manifest_entries "")
    foreach(_package IN LISTS _expected_packages)
        set(_package_dir "${_prefix}/${_libdir}/cmake/${_package}")
        file(MAKE_DIRECTORY "${_package_dir}")
        file(WRITE "${_package_dir}/${_package}Config.cmake" "# relocatable fake config\n")
        file(WRITE "${_package_dir}/${_package}Targets.cmake" "# relocatable fake targets\n")
        file(WRITE "${_package_dir}/${_package}Targets-release.cmake" "# fake per-config targets\n")
        list(APPEND _manifest_entries
            "${_package_dir}/${_package}Config.cmake"
            "${_package_dir}/${_package}Targets.cmake"
            "${_package_dir}/${_package}Targets-release.cmake"
        )
    endforeach()
    set(_resources "${_prefix}/${_datadir}/synthrt/G2pPackages")
    foreach(_package IN LISTS _g2p_packages)
        file(MAKE_DIRECTORY "${_resources}/${_package}")
        file(WRITE "${_resources}/${_package}/package.json" "{}\n")
        list(APPEND _manifest_entries "${_resources}/${_package}/package.json")
    endforeach()
    file(MAKE_DIRECTORY
        "${_resources}/Phonetic-Suite-Cmn/modules/G2p-Cmn/dict/mandarin"
        "${_resources}/Phonetic-Suite-Yue/modules/G2p-Yue/dict/cantonese"
    )
    set(_mandarin_license "${_resources}/Phonetic-Suite-Cmn/modules/G2p-Cmn/dict/mandarin/License.txt")
    set(_cantonese_license "${_resources}/Phonetic-Suite-Yue/modules/G2p-Yue/dict/cantonese/License.txt")
    file(WRITE "${_mandarin_license}" "fake license\n")
    file(WRITE "${_cantonese_license}" "fake license\n")
    list(APPEND _manifest_entries
        "${_mandarin_license}" "${_cantonese_license}"
    )
    string(REPLACE ";" "\n" _manifest_contents "${_manifest_entries}")
    set(_manifest "${_self_test_root}/install_manifest.txt")
    file(WRITE "${_manifest}" "${_manifest_contents}\n")
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            "-DINSTALL_PREFIX=${_prefix}"
            "-DINSTALL_MANIFEST=${_manifest}"
            "-DSOURCE_DIR=${_self_test_root}/source-sentinel"
            "-DBINARY_DIR=${_self_test_root}/binary-sentinel"
            "-DCMAKE_INSTALL_LIBDIR=${_libdir}"
            "-DCMAKE_INSTALL_DATADIR=${_datadir}"
            -P "${CMAKE_CURRENT_LIST_FILE}"
        RESULT_VARIABLE _self_test_result
        OUTPUT_VARIABLE _self_test_stdout
        ERROR_VARIABLE _self_test_stderr
    )
    if(NOT _self_test_result EQUAL 0)
        message(FATAL_ERROR
            "Install scanner self-test failed\n${_self_test_stdout}\n${_self_test_stderr}"
        )
    endif()

    set(_release_targets
        "${_prefix}/${_libdir}/cmake/synthrt/synthrtTargets-release.cmake"
    )
    file(WRITE "${_release_targets}" "set(leaked_path \"${_self_test_root}/source-sentinel\")\n")
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            "-DINSTALL_PREFIX=${_prefix}"
            "-DINSTALL_MANIFEST=${_manifest}"
            "-DSOURCE_DIR=${_self_test_root}/source-sentinel"
            "-DBINARY_DIR=${_self_test_root}/binary-sentinel"
            "-DCMAKE_INSTALL_LIBDIR=${_libdir}"
            "-DCMAKE_INSTALL_DATADIR=${_datadir}"
            -P "${CMAKE_CURRENT_LIST_FILE}"
        RESULT_VARIABLE _negative_result
        OUTPUT_VARIABLE _negative_stdout
        ERROR_VARIABLE _negative_stderr
    )
    if(_negative_result EQUAL 0)
        message(FATAL_ERROR "Scanner accepted a source path in Targets-release.cmake")
    endif()
    string(FIND "${_negative_stderr}" "Build-tree absolute path found" _negative_match)
    if(_negative_match EQUAL -1)
        message(FATAL_ERROR
            "Scanner failed for the wrong reason\n${_negative_stdout}\n${_negative_stderr}"
        )
    endif()
    message(STATUS "Install scanner self-test passed")
    return()
endif()

foreach(_required_variable IN ITEMS
        INSTALL_PREFIX INSTALL_MANIFEST SOURCE_DIR BINARY_DIR
        CMAKE_INSTALL_LIBDIR CMAKE_INSTALL_DATADIR)
    if(NOT DEFINED ${_required_variable} OR "${${_required_variable}}" STREQUAL "")
        message(FATAL_ERROR "${_required_variable} is required")
    endif()
endforeach()

get_filename_component(INSTALL_PREFIX "${INSTALL_PREFIX}" ABSOLUTE)
get_filename_component(INSTALL_MANIFEST "${INSTALL_MANIFEST}" ABSOLUTE)
get_filename_component(SOURCE_DIR "${SOURCE_DIR}" ABSOLUTE)
get_filename_component(BINARY_DIR "${BINARY_DIR}" ABSOLUTE)
file(TO_CMAKE_PATH "${INSTALL_PREFIX}" _prefix_normalized)
file(TO_CMAKE_PATH "${SOURCE_DIR}" _source_normalized)
file(TO_CMAKE_PATH "${BINARY_DIR}" _binary_normalized)
string(TOLOWER "${_source_normalized}" _source_compare)
string(TOLOWER "${_binary_normalized}" _binary_compare)

if(NOT IS_DIRECTORY "${INSTALL_PREFIX}")
    message(FATAL_ERROR "Install prefix does not exist: ${INSTALL_PREFIX}")
endif()
if(NOT EXISTS "${INSTALL_MANIFEST}")
    message(FATAL_ERROR "Install manifest does not exist: ${INSTALL_MANIFEST}")
endif()

set(_package_root "${INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/cmake")
if(NOT IS_DIRECTORY "${_package_root}")
    message(FATAL_ERROR "CMake package root is missing: ${_package_root}")
endif()

set(_required_manifest_paths "")
foreach(_package IN LISTS _expected_packages)
    set(_package_dir "${_package_root}/${_package}")
    set(_config "${_package_dir}/${_package}Config.cmake")
    set(_targets "${_package_dir}/${_package}Targets.cmake")
    if(NOT EXISTS "${_config}")
        message(FATAL_ERROR "Expected package config is missing: ${_config}")
    endif()
    if(NOT EXISTS "${_targets}")
        message(FATAL_ERROR "Expected package targets are missing: ${_targets}")
    endif()
    list(APPEND _required_manifest_paths "${_config}" "${_targets}")
endforeach()

file(GLOB_RECURSE _package_files LIST_DIRECTORIES FALSE "${_package_root}/*.cmake")
if(NOT _package_files)
    message(FATAL_ERROR "No CMake package files found under ${_package_root}")
endif()
foreach(_package_file IN LISTS _package_files)
    file(READ "${_package_file}" _contents)
    string(REPLACE "\\" "/" _contents "${_contents}")
    string(FIND "${_contents}" "\${_IMPORT_PREFIX}/../../include" _bad_include)
    if(NOT _bad_include EQUAL -1)
        message(FATAL_ERROR
            "Non-relocatable include path found in ${_package_file}: "
            "\${_IMPORT_PREFIX}/../../include"
        )
    endif()
    string(TOLOWER "${_contents}" _contents_compare)
    foreach(_forbidden IN ITEMS _source_compare _binary_compare)
        if(NOT "${${_forbidden}}" STREQUAL "")
            string(FIND "${_contents_compare}" "${${_forbidden}}" _reference)
            if(NOT _reference EQUAL -1)
                message(FATAL_ERROR "Build-tree absolute path found in ${_package_file}: ${${_forbidden}}")
            endif()
        endif()
    endforeach()
endforeach()

set(_resources "${INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/synthrt/G2pPackages")
foreach(_package IN LISTS _g2p_packages)
    set(_package_json "${_resources}/${_package}/package.json")
    if(NOT EXISTS "${_package_json}")
        message(FATAL_ERROR "G2P package metadata is missing: ${_package_json}")
    endif()
    list(APPEND _required_manifest_paths "${_package_json}")
endforeach()

if(EXISTS "${_resources}/Phonetic-Suite-Eng")
    message(FATAL_ERROR
        "Unverified Phonetic-Suite-Eng resources were included in the default install tree")
endif()

set(_licenses
    "${_resources}/Phonetic-Suite-Cmn/modules/G2p-Cmn/dict/mandarin/License.txt"
    "${_resources}/Phonetic-Suite-Yue/modules/G2p-Yue/dict/cantonese/License.txt"
)
foreach(_license IN LISTS _licenses)
    if(NOT EXISTS "${_license}")
        message(FATAL_ERROR "Dictionary license is missing: ${_license}")
    endif()
    list(APPEND _required_manifest_paths "${_license}")
endforeach()

file(STRINGS "${INSTALL_MANIFEST}" _manifest_entries)
set(_normalized_manifest_entries "")
foreach(_entry IN LISTS _manifest_entries)
    string(STRIP "${_entry}" _entry)
    if(_entry STREQUAL "")
        continue()
    endif()
    if(IS_ABSOLUTE "${_entry}")
        get_filename_component(_installed_path "${_entry}" ABSOLUTE)
    else()
        get_filename_component(_installed_path "${INSTALL_PREFIX}/${_entry}" ABSOLUTE)
    endif()
    file(TO_CMAKE_PATH "${_installed_path}" _installed_normalized)
    string(TOLOWER "${_installed_normalized}" _installed_compare)
    string(TOLOWER "${_prefix_normalized}" _prefix_compare)
    string(FIND "${_installed_compare}/" "${_prefix_compare}/" _prefix_position)
    if(NOT _prefix_position EQUAL 0)
        message(FATAL_ERROR "Manifest entry is outside install prefix: ${_entry}")
    endif()
    if(NOT EXISTS "${_installed_path}")
        message(FATAL_ERROR "Manifest entry does not exist: ${_entry}")
    endif()
    list(APPEND _normalized_manifest_entries "${_installed_normalized}")
endforeach()

foreach(_required_path IN LISTS _required_manifest_paths)
    get_filename_component(_required_path "${_required_path}" ABSOLUTE)
    file(TO_CMAKE_PATH "${_required_path}" _required_normalized)
    list(FIND _normalized_manifest_entries "${_required_normalized}" _manifest_index)
    if(_manifest_index EQUAL -1)
        message(FATAL_ERROR "Required installed file is absent from manifest: ${_required_path}")
    endif()
endforeach()

message(STATUS "Install tree checks passed: ${INSTALL_PREFIX}")
