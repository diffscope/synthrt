#[[
    BuildAPI.cmake

    Include this file to generate build APIs for your current project.

        Variable                          Default Candidates
    ----------------------------------------------------------------------------------------------------
    Constants:
        proj:                             ${PROJECT_NAME}
        year:                             current year
    
    Variables:
        prefix:                           <proj>_VAR_PREFIX, <proj-upcase>

    Options:
        <prefix>_VERSION:                 ${PROJECT_VERSION}, 0.0.0.0
        <prefix>_DESCRIPTION:             ${PROJECT_DESCRIPTION}, <proj>
        <prefix>_AUTHOR:                  "<proj> Developers"
        <prefix>_START_YEAR:              <NONE>
        <prefix>_COPYRIGHT:               "Copyright (c) ${<proj>_START_YEAR}-<year> ${<proj>_AUTHOR}",
                                          "Copyright (c) <year> ${<proj>_AUTHOR}"
        <prefix>_INSTALL_NAME:            <proj>
        <prefix>_INCLUDE_DIR:             <NONE>
        <prefix>_BUILD_INCLUDE_DIR:       "${CMAKE_CURRENT_BINARY_DIR}/../etc/include"
        <prefix>_GENERATED_INCLUDE_DIR:   "${CMAKE_CURRENT_BINARY_DIR}/../include"
        <prefix>_CONFIG_TEMPLATE:         <proj>Config.cmake.in
        <prefix>_TARGET_PREFIX:           <proj>
        <prefix>_MACRO_PREFIX:            <proj>

        <prefix>_INSTALL:                 FALSE
        <prefix>_BUILD_SHARED:            FALSE
        <prefix>_SYNC_INCLUDE:            FALSE
    ----------------------------------------------------------------------------------------------------
    
    Macros/Functions:
         <name>_add_executable
         <name>_add_library
         <name>_add_plugin
         <name>_install
]] #

include_guard(DIRECTORY)

qm_import(Preprocess)

# ----------------------------------
# Project Variables
# ----------------------------------
set(_CUR_VERSION)
set(_CUR_DESCRIPTION)
set(_CUR_AUTHOR)
set(_CUR_START_YEAR)
set(_CUR_COPYRIGHT)
set(_CUR_INSTALL_NAME)

# ----------------------------------
# Project Directories
# ----------------------------------
set(_CUR_INCLUDE_DIR) # Nullable
set(_CUR_BUILD_INCLUDE_DIR)
set(_CUR_GENERATED_INCLUDE_DIR)

# ----------------------------------
# Project Options
# ----------------------------------
set(_CUR_INSTALL FALSE)
set(_CUR_BUILD_SHARED FALSE)
set(_CUR_SYNC_INCLUDE FALSE)

# ----------------------------------
# Other Option Variables
# ----------------------------------
set(_CUR_CONFIG_TEMPLATE)
set(_CUR_TARGET_PREFIX)
set(_CUR_MACRO_PREFIX)

if(NOT PROJECT_NAME)
    message(FATAL_ERROR "PROJECT_NAME not set")
endif()

# ----------------------------------
# Variables
# ----------------------------------
set(_CUR_NAME ${PROJECT_NAME})
set(_CUR_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
string(TIMESTAMP _CUR_BUILD_YEAR "%Y" UTC)

# Set default values
qm_set_value(_CUR_PREFIX ${_CUR_NAME}_VAR_PREFIX ${_CUR_NAME})
string(TOUPPER ${_CUR_PREFIX} _CUR_PREFIX_UPPER)

qm_set_value(_CUR_VERSION ${_CUR_PREFIX_UPPER}_VERSION PROJECT_VERSION "0.0.0.0")
qm_set_value(_CUR_DESCRIPTION ${_CUR_PREFIX_UPPER}_DESCRIPTION PROJECT_DESCRIPTION "${_CUR_NAME}")
qm_set_value(_CUR_AUTHOR ${_CUR_PREFIX_UPPER}_AUTHOR "${_CUR_NAME} Developers")

if(${_CUR_PREFIX_UPPER}_START_YEAR)
    set(_CUR_START_YEAR ${${_CUR_PREFIX_UPPER}_START_YEAR})
endif()

if(_CUR_START_YEAR)
    set(_CUR_COPYRIGHT "Copyright (c) ${_CUR_START_YEAR}-${_CUR_BUILD_YEAR} ${_CUR_AUTHOR}")
else()
    set(_CUR_COPYRIGHT "Copyright (c) ${_CUR_BUILD_YEAR} ${_CUR_AUTHOR}")
endif()

qm_set_value(_CUR_COPYRIGHT ${_CUR_PREFIX_UPPER}_COPYRIGHT "${_CUR_COPYRIGHT}")
qm_set_value(_CUR_INSTALL_NAME ${_CUR_PREFIX_UPPER}_INSTALL_NAME "${_CUR_NAME}")

if(${_CUR_PREFIX_UPPER}_INCLUDE_DIR)
    set(_CUR_INCLUDE_DIR ${${_CUR_PREFIX_UPPER}_INCLUDE_DIR})
endif()

qm_set_value(_CUR_BUILD_INCLUDE_DIR ${_CUR_PREFIX_UPPER}_BUILD_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/../etc/include")
qm_set_value(_CUR_GENERATED_INCLUDE_DIR ${_CUR_PREFIX_UPPER}_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/../include")

if(${_CUR_PREFIX_UPPER}_INSTALL)
    set(_CUR_INSTALL TRUE)
endif()

if(${_CUR_PREFIX_UPPER}_BUILD_SHARED)
    set(_CUR_BUILD_SHARED TRUE)
endif()

if(${_CUR_PREFIX_UPPER}_SYNC_INCLUDE)
    set(_CUR_SYNC_INCLUDE TRUE)
endif()

qm_set_value(_CUR_CONFIG_TEMPLATE ${_CUR_PREFIX_UPPER}_CONFIG_TEMPLATE "${_CUR_NAME}Config.cmake.in")
qm_set_value(_CUR_TARGET_PREFIX ${_CUR_PREFIX_UPPER}_TARGET_PREFIX "${_CUR_NAME}")
qm_set_value(_CUR_MACRO_PREFIX ${_CUR_PREFIX_UPPER}_MACRO_PREFIX "${_CUR_PREFIX}")

# ----------------------------------
# Prepare
# ----------------------------------
if(_CUR_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)
endif()

# ----------------------------------
# Declare macros
# ----------------------------------

#[[
    Add executable target.

    <name>_add_executable(<target>
        [SYNC_INCLUDE_PREFIX  <prefix>]
        [SYNC_INCLUDE_OPTIONS <options...>]
        [NO_SYNC_INCLUDE]
        [RC_NAME <name>]
        [RC_DESCRIPTION <description>]
        [RC_COPYRIGHT <copyright>]
        [NO_WIN_RC]
        [NO_EXPORT]
        <configure_options...>
    )
]] #
macro(${_CUR_MACRO_PREFIX}_add_executable _target)
    set(options SYNC_INCLUDE NO_SYNC_INCLUDE NO_WIN_RC NO_EXPORT)
    set(oneValueArgs SYNC_INCLUDE_PREFIX RC_NAME RC_DESCRIPTION RC_COPYRIGHT)
    set(multiValueArgs SYNC_INCLUDE_OPTIONS)
    cmake_parse_arguments(FUNC "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    add_executable(${_target})

    qm_set_value(_rc_name FUNC_RC_NAME ${_CUR_INSTALL_NAME})
    qm_set_value(_rc_description FUNC_RC_DESCRIPTION ${_CUR_DESCRIPTION})
    qm_set_value(_rc_copyright FUNC_RC_COPYRIGHT ${_CUR_COPYRIGHT})

    if(WIN32 AND NOT FUNC_NO_WIN_RC)
        qm_add_win_rc(${_target}
            NAME ${_rc_name}
            DESCRIPTION ${_rc_description}
            COPYRIGHT ${_rc_copyright}
        )
    endif()

    # Configure target
    qm_configure_target(${_target} ${FUNC_UNPARSED_ARGUMENTS})

    # Add include directories
    target_include_directories(${_target} PRIVATE ${_CUR_SOURCE_DIR}/${_CUR_INCLUDE_DIR})
    target_include_directories(${_target} PRIVATE ${_CUR_BUILD_INCLUDE_DIR})
    target_include_directories(${_target} PRIVATE .)

    # Library name
    if(_target MATCHES "^${_CUR_NAME}(.+)")
        set(_name ${CMAKE_MATCH_1})
        set_target_properties(${_target} PROPERTIES EXPORT_NAME ${_name})
    else()
        set(_name ${_target})
    endif()

    add_executable(${_CUR_INSTALL_NAME}::${_name} ALIAS ${_target})

    if(FUNC_SYNC_INCLUDE_PREFIX)
        set(_inc_name ${FUNC_SYNC_INCLUDE_PREFIX})
    else()
        set(_inc_name ${_target})
    endif()

    if(_CUR_INSTALL)
        if(FUNC_NO_EXPORT)
            set(_export)
        else()
            set(_export EXPORT ${_CUR_INSTALL_NAME}Targets)
        endif()

        install(TARGETS ${_target}
            ${_export}
            RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" OPTIONAL
        )
    endif()

    if(FUNC_SYNC_INCLUDE OR(_CUR_SYNC_INCLUDE AND NOT FUNC_NO_SYNC_INCLUDE))
        # Generate a standard include directory in build directory
        qm_sync_include(. "${_CUR_GENERATED_INCLUDE_DIR}/${_inc_name}" ${_install_options}
            ${FUNC_SYNC_INCLUDE_OPTIONS} FORCE
        )
        target_include_directories(${_target} PUBLIC ${_CUR_GENERATED_INCLUDE_DIR}>)
    endif()
endmacro()

#[[
    Add library target.

    <name>_add_library(<target>
        [SHARED | STATIC]
        [PREFIX <prefix>]
        [SYNC_INCLUDE_PREFIX  <prefix>]
        [SYNC_INCLUDE_OPTIONS <options...>]
        [NO_SYNC_INCLUDE]
        [RC_NAME <name>]
        [RC_DESCRIPTION <description>]
        [RC_COPYRIGHT <copyright>]
        [NO_WIN_RC]
        [NO_EXPORT]
        <configure_options...>
    )
]] #
macro(${_CUR_MACRO_PREFIX}_add_library _target)
    set(options SHARED STATIC)
    set(oneValueArgs)
    set(multiValueArgs)
    cmake_parse_arguments(FUNC "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(FUNC_SHARED)
        set(_type SHARED)
    elseif(FUNC_STATIC)
        set(_type STATIC)
    elseif(_CUR_BUILD_SHARED)
        set(_type SHARED)
    elseif(BUILD_SHARED_LIBS)
        set(_type SHARED)
    else()
        set(_type STATIC)
    endif()

    _cur_add_library_internal(${_target} ${_type} ${FUNC_UNPARSED_ARGUMENTS})
endmacro()

#[[
    Add plugin target.

    <name>_add_plugin(<target> <category>
        [PREFIX <prefix>]
        [SYNC_INCLUDE_PREFIX  <prefix>]
        [SYNC_INCLUDE_OPTIONS <options...>]
        [RC_NAME <name>]
        [RC_DESCRIPTION <description>]
        [RC_COPYRIGHT <copyright>]
        [NO_SYNC_INCLUDE]
        [NO_WIN_RC]
        [NO_EXPORT]
        <configure_options...>
    )
]] #
macro(${_CUR_MACRO_PREFIX}_add_plugin _target _category)
    set(_plugin_dir plugins/${_CUR_INSTALL_NAME}/${_category})
    _cur_add_library_internal(${_target} SHARED
        BUILD_RUNTIME_DIR "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/${_plugin_dir}"
        BUILD_LIBRARY_DIR "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/${_plugin_dir}"
        BUILD_ARCHIVE_DIR "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/${_plugin_dir}"
        INSTALL_RUNTIME_DIR "${CMAKE_INSTALL_LIBDIR}/${_plugin_dir}"
        INSTALL_LIBRARY_DIR "${CMAKE_INSTALL_LIBDIR}/${_plugin_dir}"
        INSTALL_ARCHIVE_DIR "${CMAKE_INSTALL_LIBDIR}/${_plugin_dir}"
        ${ARGN}
    )
endmacro()

#[[
    Install targets, CMake configuration files and include files.

    <name>_install()
]] #
function(${_CUR_MACRO_PREFIX}_install)
    if(NOT _CUR_INSTALL)
        return()
    endif()

    qm_basic_install(
        NAME ${_CUR_INSTALL_NAME}
        VERSION ${_CUR_VERSION}
        INSTALL_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/${_CUR_INSTALL_NAME}
        CONFIG_TEMPLATE "${_CUR_CONFIG_TEMPLATE}"
        NAMESPACE ${_CUR_INSTALL_NAME}::
        EXPORT ${_CUR_INSTALL_NAME}Targets
        WRITE_CONFIG_OPTIONS NO_CHECK_REQUIRED_COMPONENTS_MACRO
    )

    if(_CUR_INCLUDE_DIR)
        get_filename_component(_dir ${_CUR_INCLUDE_DIR} ABSOLUTE BASE_DIR ${_CUR_SOURCE_DIR})
        install(DIRECTORY ${_dir}/
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
            FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp" PATTERN "*.hxx"
        )
    endif()
endfunction()

# ----------------------------------
# Private
# ----------------------------------
macro(_cur_add_library_internal _target _type)
    set(options SYNC_INCLUDE NO_SYNC_INCLUDE NO_WIN_RC NO_EXPORT)
    set(oneValueArgs SYNC_INCLUDE_PREFIX PREFIX RC_NAME RC_DESCRIPTION RC_COPYRIGHT
        BUILD_RUNTIME_DIR BUILD_LIBRARY_DIR BUILD_ARCHIVE_DIR
        INSTALL_RUNTIME_DIR INSTALL_LIBRARY_DIR INSTALL_ARCHIVE_DIR
    )
    set(multiValueArgs SYNC_INCLUDE_OPTIONS)
    cmake_parse_arguments(FUNC "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    add_library(${_target} SHARED)

    qm_set_value(_rc_name FUNC_RC_NAME ${_CUR_INSTALL_NAME})
    qm_set_value(_rc_description FUNC_RC_DESCRIPTION ${_CUR_DESCRIPTION})
    qm_set_value(_rc_copyright FUNC_RC_COPYRIGHT ${_CUR_COPYRIGHT})

    if(WIN32 AND NOT FUNC_NO_WIN_RC)
        qm_add_win_rc(${_target}
            NAME ${_rc_name}
            DESCRIPTION ${_rc_description}
            COPYRIGHT ${_rc_copyright}
        )
    endif()

    if(FUNC_PREFIX)
        set(_prefix_option PREFIX ${FUNC_PREFIX})
    else()
        set(_prefix_option)
    endif()

    # Set global definitions
    qm_export_defines(${_target} ${_prefix_option})

    # Configure target
    qm_configure_target(${_target} ${FUNC_UNPARSED_ARGUMENTS})

    # Add include directories
    if(_CUR_INCLUDE_DIR)
        target_include_directories(${_target} PUBLIC
            $<BUILD_INTERFACE:${_CUR_SOURCE_DIR}/${_CUR_INCLUDE_DIR}>
        )
    endif()

    target_include_directories(${_target} PRIVATE ${_CUR_BUILD_INCLUDE_DIR})
    target_include_directories(${_target} PRIVATE .)

    # Library name
    if(_target MATCHES "^${_CUR_NAME}(.+)")
        set(_name ${CMAKE_MATCH_1})
        set_target_properties(${_target} PROPERTIES EXPORT_NAME ${_name})
    else()
        set(_name ${_target})
    endif()

    add_library(${_CUR_INSTALL_NAME}::${_name} ALIAS ${_target})

    # Build output directories
    set(_build_output_dir_options)

    if(FUNC_BUILD_RUNTIME_DIR)
        list(APPEND _build_output_dir_options RUNTIME_OUTPUT_DIRECTORY ${FUNC_BUILD_RUNTIME_DIR})
    endif()

    if(FUNC_BUILD_LIBRARY_DIR)
        list(APPEND _build_output_dir_options LIBRARY_OUTPUT_DIRECTORY ${FUNC_BUILD_LIBRARY_DIR})
    endif()

    if(FUNC_BUILD_ARCHIVE_DIR)
        list(APPEND _build_output_dir_options ARCHIVE_OUTPUT_DIRECTORY ${FUNC_BUILD_ARCHIVE_DIR})
    endif()

    if(_build_output_dir_options)
        set_target_properties(${_target} PROPERTIES ${_build_output_dir_options})
    endif()

    if(FUNC_SYNC_INCLUDE_PREFIX)
        set(_inc_name ${FUNC_SYNC_INCLUDE_PREFIX})
    else()
        set(_inc_name ${_target})
    endif()

    set(_install_options)

    if(_CUR_INSTALL)
        # Install directories
        qm_set_value(_install_runtime_dir FUNC_INSTALL_RUNTIME_DIR "${CMAKE_INSTALL_BINDIR}")
        qm_set_value(_install_library_dir FUNC_INSTALL_LIBRARY_DIR "${CMAKE_INSTALL_LIBDIR}")
        qm_set_value(_install_archive_dir FUNC_INSTALL_ARCHIVE_DIR "${CMAKE_INSTALL_LIBDIR}")

        if(FUNC_NO_EXPORT)
            set(_export)
        else()
            set(_export
                EXPORT ${_CUR_INSTALL_NAME}Targets
                ARCHIVE DESTINATION "${_install_archive_dir}" OPTIONAL
            )
        endif()

        install(TARGETS ${_target}
            ${_export}
            RUNTIME DESTINATION "${_install_runtime_dir}" OPTIONAL
            LIBRARY DESTINATION "${_install_library_dir}" OPTIONAL
        )

        target_include_directories(${_target} PUBLIC
            "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
            "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/${_CUR_INCLUDE_DIR}>"
        )

        set(_install_options
            INSTALL_DIR "${CMAKE_INSTALL_INCLUDEDIR}/${_CUR_INSTALL_NAME}/${_inc_name}"
        )
    endif()

    if(FUNC_SYNC_INCLUDE OR(_CUR_SYNC_INCLUDE AND NOT FUNC_NO_SYNC_INCLUDE))
        # Generate a standard include directory in build directory
        qm_sync_include(. "${_CUR_GENERATED_INCLUDE_DIR}/${_inc_name}" ${_install_options}
            ${FUNC_SYNC_INCLUDE_OPTIONS} FORCE
        )
        target_include_directories(${_target} PUBLIC
            "$<BUILD_INTERFACE:${_CUR_GENERATED_INCLUDE_DIR}>"
        )
    endif()
endmacro()