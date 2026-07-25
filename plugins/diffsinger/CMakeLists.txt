project(dsinfer-plugins
    VERSION ${PROJECT_VERSION}
    LANGUAGES CXX
)

synthrt_declare_target(DSINFER "diffsinger" ${PROJECT_VERSION} "Team OpenVPI" 2023 "../../include")

set(CURRENT_PLUGIN_CATEGORY inferenceinterpreters)
add_subdirectory(duration)
add_subdirectory(pitch)
add_subdirectory(variance)
add_subdirectory(acoustic)
add_subdirectory(vocoder)

set(CURRENT_PLUGIN_CATEGORY singerproviders)
add_subdirectory(singer-provider)
