# inferutil: ds-infer 域内部通用实现库（AD-09 跨边界契约）。
#
# 契约性质（详见同目录 README.md）：
# - 本库为内部 lib（INFRA-01），不是公共 Level API（ARCH-02）。
# - NO_INSTALL：仅构建树可见，不随 srt-ds::infer 发布。
# - 下游 5 个 in-tree 推理插件（acoustic/duration/pitch/variance/vocoder）
#   通过 $<BUILD_INTERFACE:inferutil> 链接，表达"同源构建期依赖、不跨版本"。
# - plugins 运行期对外契约是 srt::svs::Inference 公共 Level API，不经过本库。
# - 头文件签名变更须同步修改全部 plugin 与 unittests，保证同源树编译通过。
project(inferutil
    VERSION ${DSINFER_VERSION}
    LANGUAGES CXX
)

file(GLOB_RECURSE _src *.h *.cpp)

synthrt_add_library(${PROJECT_NAME} STATIC NO_INSTALL
    SOURCES ${_src}
    FEATURES cxx_std_20
    LINKS srt-ds::infer
    INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_SOURCE_DIR}/include/diffsinger/Infer)
