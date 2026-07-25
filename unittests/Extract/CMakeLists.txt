# Extract 单元测试
#
# tst_extractor_skeleton.cpp 是保留的骨架测试（仅 SUCCEED），保证 target
# 即使在没有 GLOB 匹配文件时也能注册。
synthrt_add_unittest(synthrt-unittest-extract tst_extractor_skeleton.cpp)

# tst_extractor_no_category.cpp 单独编译为独立 target：该文件验证
# EX-002（Runtime 无 "inference" module category）。其他 tst_*.cpp 通过
# ModuleCategoryRegistrar<MockInferenceCategory> 全局注册了一个 inference
# category 工厂（Runtime::Impl::moduleCategoryFactories 是进程级静态列表），
# 会污染同 target 内的所有 Runtime 实例，使 EX-002 不可达。独立 target
# 拥有自己的全局状态，不注册任何 category 工厂。
synthrt_add_unittest(synthrt-unittest-extract-nocat tst_extractor_no_category.cpp)
target_link_libraries(synthrt-unittest-extract-nocat PRIVATE srt::extract srt::core)

# 额外的 Extract 单元测试：通过 GLOB 自动纳入 tst_*.cpp（排除 skeleton 与
# no_category）。这些测试注册了 MockInferenceCategory 全局工厂，可注入
# mock InferenceDriver 到 Runtime 的 inference category。
file(GLOB _extract_tests CONFIGURE_DEPENDS "tst_*.cpp")
list(REMOVE_ITEM _extract_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/tst_extractor_skeleton.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tst_extractor_no_category.cpp")
if(_extract_tests)
    synthrt_add_unittest(synthrt-unittest-extract-ext "${_extract_tests}")
    target_link_libraries(synthrt-unittest-extract-ext PRIVATE srt::extract srt::driver srt::core)
endif()
