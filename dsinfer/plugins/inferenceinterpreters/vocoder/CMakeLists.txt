project(vocoder
    VERSION ${DSINFER_VERSION}
    LANGUAGES CXX
)

dsinfer_add_interpreter_plugin(${PROJECT_NAME} LINKS_PRIVATE $<BUILD_INTERFACE:inferutil>)
