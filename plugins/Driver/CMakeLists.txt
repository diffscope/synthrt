project(srt-driver-plugins
    VERSION ${PROJECT_VERSION}
    LANGUAGES CXX
)

synthrt_declare_target(srt_driver "srt-driver" ${PROJECT_VERSION} "Team OpenVPI" 2023 "../../include")

add_subdirectory(onnx)
