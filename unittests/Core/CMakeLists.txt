synthrt_add_unittest(synthrt-unittest-core tst_core_skeleton.cpp)
synthrt_add_unittest(synthrt-unittest-plugin tst_plugin_skeleton.cpp)

file(GLOB _core_runtime_tests CONFIGURE_DEPENDS "tst_*.cpp")
if(_core_runtime_tests)
    synthrt_add_unittest(synthrt-unittest-core-runtime "${_core_runtime_tests}")
    target_link_libraries(synthrt-unittest-core-runtime PRIVATE srt::core srt::g2p)
endif()
