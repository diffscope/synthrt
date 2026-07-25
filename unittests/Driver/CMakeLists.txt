# Driver 单元测试
#
# tst_driver_skeleton.cpp 是保留的骨架测试（仅 REQUIRE(true)），保证 target
# 即使在没有 GLOB 匹配文件时也能注册。
synthrt_add_unittest(synthrt-unittest-driver tst_driver_skeleton.cpp)

# 额外的 Driver 单元测试：通过 GLOB 自动纳入 tst_*.cpp。
# 这些测试可能引用 OnnxDriverApi.h / OnnxSetup.h，因此需要链接 srt::driver
# 才能解析 ExecutionProvider 等符号。
file(GLOB _driver_tests CONFIGURE_DEPENDS "tst_*.cpp")
if(_driver_tests)
    synthrt_add_unittest(synthrt-unittest-driver-ext "${_driver_tests}")
    target_link_libraries(synthrt-unittest-driver-ext PRIVATE srt::driver)
endif()
