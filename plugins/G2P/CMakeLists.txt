project(srt-g2p-plugins
    VERSION ${PROJECT_VERSION}
    LANGUAGES CXX
)

synthrt_declare_target(srt_g2p "srt-g2p" ${PROJECT_VERSION} "Team OpenVPI" 2023 "../../include")

add_subdirectory(mandarin)
add_subdirectory(cantonese)
add_subdirectory(multig2p)
add_subdirectory(chain)
add_subdirectory(ds-dict)
