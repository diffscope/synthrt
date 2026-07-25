# unittests/tools/CMakeLists.txt
#
# TD-CLI-01: CLI 工具测试基础设施。
#
# 为 tools/ 下 3 个 CLI 工具（dsinfer-cli / dspk-pack-cli / s2p-cli）建立
# 黑盒 subprocess 测试目标。所有测试复用 catch2 v3 与 synthrt_add_unittest
# 命名约定（synthrt-unittest-<tool>），不修改 CLI 源码接口（D-11）。
#
# 测试分级（INFRA-03）：
# - L2：通过 subprocess 启动可执行文件，校验退出码 / stdout / stderr
# - 不依赖插件 DLL 加载，与 L1 共享 catch2 v3 框架
#
# 依赖关系：
# - 各 CLI 工具可执行文件必须先构建（add_dependencies 保障）
# - 测试 fixture（最小 MIDI / DSPX / dictionary.tsv）由 tests/fixtures/cli/ 提供
#
# 注：synthrt_add_unittest 默认链接 Catch2::Catch2WithMain。
# 测试仅通过 subprocess 调用可执行文件，无需链接 srt::core / srt-ds::bank /
# srt::s2p 库 API（避免引入额外的传递依赖与潜在 DLL 部署）。

# dsinfer-cli 黑盒测试（L2）
synthrt_add_unittest(synthrt-unittest-dsinfer-cli
    tst_dsinfer_cli.cpp
)
target_compile_definitions(synthrt-unittest-dsinfer-cli PRIVATE
    SYNTHRT_TEST_CLI_EXE="$<TARGET_FILE:dsinfer-cli>"
)
add_dependencies(synthrt-unittest-dsinfer-cli dsinfer-cli)

# dspk-pack-cli 黑盒测试（L2）
synthrt_add_unittest(synthrt-unittest-dspk-pack-cli
    tst_dspk_pack_cli.cpp
)
target_compile_definitions(synthrt-unittest-dspk-pack-cli PRIVATE
    SYNTHRT_TEST_CLI_EXE="$<TARGET_FILE:dspk-pack-cli>"
)
add_dependencies(synthrt-unittest-dspk-pack-cli dspk-pack-cli)

# s2p-cli 黑盒测试（L2）
synthrt_add_unittest(synthrt-unittest-s2p-cli
    tst_s2p_cli.cpp
)
target_compile_definitions(synthrt-unittest-s2p-cli PRIVATE
    SYNTHRT_TEST_CLI_EXE="$<TARGET_FILE:s2p-cli>"
)
add_dependencies(synthrt-unittest-s2p-cli s2p-cli)

# dsinfer-cli 内部单元测试（L1，GAP-NEW-004）
# 直接编译 tools/dsinfer-cli/segment_builder.cpp 以访问内部纯逻辑函数。
# note_data.h 引入 srt::core / srt-ds::infer 重型依赖，但测试本身仅校验
# segment_builder 的纯逻辑（无运行时调用 lib/ API）。
synthrt_add_unittest(synthrt-unittest-dsinfer-cli-internals
    tst_dsinfer_cli_internals.cpp
    "${CMAKE_SOURCE_DIR}/tools/dsinfer-cli/segment_builder.cpp"
)
target_link_libraries(synthrt-unittest-dsinfer-cli-internals PRIVATE
    srt::core
    srt-ds::infer
    srt::g2p
    srt::s2p
)
target_include_directories(synthrt-unittest-dsinfer-cli-internals PRIVATE
    "${CMAKE_SOURCE_DIR}/tools/dsinfer-cli"
    "${CMAKE_SOURCE_DIR}/include/diffsinger/Infer"
)

# cli_log 内部单元测试（L1，GAP-NEW-006）
# 直接编译 tools/dsinfer-cli/cli_log.cpp 验证 P0-3 诊断日志级别语义。
# 通过 Logger::setLogCallback 注入捕获 callback，不依赖 stdout 重定向。
# cli_log.cpp 依赖 srt::g2p::kG2pModeCopy（LangCommon.h），需链接 srt::g2p。
# 注：installLogCallback/uninstallLogCallback 会修改全局 Logger callback，
# 测试必须在结束时调用 uninstallLogCallback 清理状态。
synthrt_add_unittest(synthrt-unittest-cli-log
    tst_cli_log.cpp
    "${CMAKE_SOURCE_DIR}/tools/dsinfer-cli/cli_log.cpp"
)
target_link_libraries(synthrt-unittest-cli-log PRIVATE
    srt::core
    srt::g2p
)
target_include_directories(synthrt-unittest-cli-log PRIVATE
    "${CMAKE_SOURCE_DIR}/tools/dsinfer-cli"
)

# dspx_parser 内部单元测试（L1，GAP-NEW-007）
# 通过 #include "dspx_parser.cpp" 单文件编译访问 static 函数 dspxTickToMs。
# 条件编译：当且仅当 opendspx::opendspxserializer target 存在时构建，
# 与生产代码（tools/dsinfer-cli/CMakeLists.txt）的 DSINFER_CLI_HAS_OPENDSPX
# 条件严格一致。
#
# 当前构建未启用 opendspx 时，本测试目标不会构建（与生产路径一致）：
#   - parseDspx 在 dsinfer-cli.exe 中实际不可达
#   - dspxTickToMs 仅在 parseDspx 内被调用，同样不可达
#   - "dspx 模式不崩溃" 已由 GAP-NEW-001 黑盒测试覆盖（未启用路径）
# 当 opendspx 启用时（CI 或开发者配置），本测试目标自动构建并验证
# BUG-CLI-007（BPM=0 除零）/ BUG-CLI-008（tick 截断）修复。
if(TARGET opendspx::opendspxserializer)
    synthrt_add_unittest(synthrt-unittest-dspx-parser
        tst_dspx_parser.cpp
    )
    target_compile_definitions(synthrt-unittest-dspx-parser PRIVATE
        DSINFER_CLI_HAS_OPENDSPX
    )
    target_link_libraries(synthrt-unittest-dspx-parser PRIVATE
        srt::core
        srt::g2p
        opendspx::opendspxserializer
    )
    target_include_directories(synthrt-unittest-dspx-parser PRIVATE
        "${CMAKE_SOURCE_DIR}/tools/dsinfer-cli"
    )
endif()

# midi_parser 内部单元测试（L1，GAP-NEW-008）
# 直接编译 tools/dsinfer-cli/midi_parser.cpp 验证 parseMidi 公开函数。
# 测试通过构造最小 MIDI 字节序列写入临时文件，调用 parseMidi 验证返回结果。
# note_data.h 引入 srt::core / srt-ds::infer 重型依赖（与 internals 测试一致）。
synthrt_add_unittest(synthrt-unittest-midi-parser
    tst_midi_parser.cpp
    "${CMAKE_SOURCE_DIR}/tools/dsinfer-cli/midi_parser.cpp"
)
target_link_libraries(synthrt-unittest-midi-parser PRIVATE
    srt::core
    srt-ds::infer
    srt::g2p
    srt::s2p
)
target_include_directories(synthrt-unittest-midi-parser PRIVATE
    "${CMAKE_SOURCE_DIR}/tools/dsinfer-cli"
    "${CMAKE_SOURCE_DIR}/include/diffsinger/Infer"
)

# wav_writer 内部单元测试（L1，GAP-NEW-010）
# 直接编译 tools/dsinfer-cli/wav_writer.cpp 验证 writeWav 公开函数。
# 构造 ds::infer::InferenceResult 写入临时文件，验证返回值与文件存在性。
# wav_writer.cpp 依赖 cli_log.cpp + srt::audio (AudioFileWriter, libsndfile backend)。
synthrt_add_unittest(synthrt-unittest-wav-writer
    tst_wav_writer.cpp
    "${CMAKE_SOURCE_DIR}/tools/dsinfer-cli/wav_writer.cpp"
    "${CMAKE_SOURCE_DIR}/tools/dsinfer-cli/cli_log.cpp"
)
target_link_libraries(synthrt-unittest-wav-writer PRIVATE
    srt::core
    srt-ds::infer
    srt::g2p
    srt::audio
)
target_include_directories(synthrt-unittest-wav-writer PRIVATE
    "${CMAKE_SOURCE_DIR}/tools/dsinfer-cli"
    "${CMAKE_SOURCE_DIR}/include/diffsinger/Infer"
)
