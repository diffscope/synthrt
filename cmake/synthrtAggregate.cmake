include_guard(DIRECTORY)

# synthrt_setup_aggregate: declare an aggregate INTERFACE target with a build-tree
# alias and (when SYNTHRT_INSTALL) register it in the synthrtTargets export set.
#
# Usage:
#   include(synthrtAggregate)
#   synthrt_setup_aggregate(
#       TARGET synthrt-synthrt
#       ALIAS srt::synthrt
#       EXPORT_NAME synthrt
#       FOLDER "Domains"
#       COMPONENTS srt::core srt::driver srt::s2p srt::svs srt-ds::bank srt-ds::session
#   )
#
# The install(EXPORT synthrtTargets ...) / configure_package_config_file /
# write_basic_package_version_file / resource install calls remain in the
# top-level CMakeLists.txt since they are config-level, not per-target.

function(synthrt_setup_aggregate)
    set(options)
    set(oneValueArgs TARGET ALIAS EXPORT_NAME FOLDER)
    set(multiValueArgs COMPONENTS)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT ARG_TARGET)
        message(FATAL_ERROR "synthrt_setup_aggregate requires TARGET argument")
    endif()

    # Create the aggregate INTERFACE target if it does not exist yet.
    if(NOT TARGET ${ARG_TARGET})
        add_library(${ARG_TARGET} INTERFACE)
    endif()

    # Build-tree alias (install alias comes from NAMESPACE in install(EXPORT)).
    if(ARG_ALIAS)
        if(NOT TARGET ${ARG_ALIAS})
            add_library(${ARG_ALIAS} ALIAS ${ARG_TARGET})
        endif()
    endif()

    # Link constituent libraries into the aggregate.
    if(ARG_COMPONENTS)
        target_link_libraries(${ARG_TARGET} INTERFACE ${ARG_COMPONENTS})
    endif()

    # Folder grouping (IDE / cmake-gui).
    if(ARG_FOLDER)
        set_target_properties(${ARG_TARGET} PROPERTIES FOLDER ${ARG_FOLDER})
    endif()

    # Register in the synthrtTargets export set.
    if(SYNTHRT_INSTALL)
        if(ARG_EXPORT_NAME)
            set_target_properties(${ARG_TARGET} PROPERTIES EXPORT_NAME ${ARG_EXPORT_NAME})
        endif()
        install(TARGETS ${ARG_TARGET}
            EXPORT synthrtTargets
        )
    endif()
endfunction()
