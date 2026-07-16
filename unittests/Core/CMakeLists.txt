synthrt_add_unittest(synthrt-unittest-core CoreSkeletonTest.cpp)
synthrt_add_unittest(synthrt-unittest-plugin PluginSkeletonTest.cpp)

file(GLOB _core_runtime_tests CONFIGURE_DEPENDS "test_*.cpp")
if(_core_runtime_tests)
    synthrt_add_unittest(synthrt-unittest-core-runtime "${_core_runtime_tests}")
    target_link_libraries(synthrt-unittest-core-runtime PRIVATE synthrt::core srt::g2p)
endif()
