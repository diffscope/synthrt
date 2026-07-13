if(NOT SYNTHRT_ENABLE_LUAJIT)
    synthrt_add_unittest(synthrt-unittest-s2p test_lua_disabled.cpp)
    target_link_libraries(synthrt-unittest-s2p PRIVATE srt::s2p)
endif()
