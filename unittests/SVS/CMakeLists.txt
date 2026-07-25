# SVS 单元测试
#
# tst_svs_skeleton.cpp 是保留的骨架测试（仅 REQUIRE(true)），保证 target
# 即使在没有 GLOB 匹配文件时也能注册。
synthrt_add_unittest(synthrt-unittest-svs tst_svs_skeleton.cpp)

# 额外的 SVS 单元测试：通过 GLOB 自动纳入 tst_*.cpp。
file(GLOB _svs_tests CONFIGURE_DEPENDS "tst_*.cpp")
if(_svs_tests)
    synthrt_add_unittest(synthrt-unittest-svs-ext "${_svs_tests}")
    target_link_libraries(synthrt-unittest-svs-ext PRIVATE srt::svs)
endif()
