project(diffsinger
    VERSION ${DSINFER_VERSION}
    LANGUAGES CXX
)

dsinfer_add_interpreter_plugin(${PROJECT_NAME})
