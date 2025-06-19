project(wavfile)

dsinfer_add_library(${PROJECT_NAME} INTERFACE NO_INSTALL
    INCLUDE_INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/include
)