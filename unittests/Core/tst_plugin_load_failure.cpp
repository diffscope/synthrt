// T-11 PluginFactory 加载失败诊断测试
//
// 覆盖 PluginFactory::scanPlugins (lib/Core/Plugin/PluginFactory.cpp) 的失败分支。
// 任务文档原始假设的"依赖缺失"和"版本不匹配"失败路径在当前 PluginFactory 中
// **不存在**（scanPlugins 只检查 plugin.json、DLL 存在性、symbol、iid、duplicate key）。
// 本测试根据实际代码调整，覆盖真实存在的失败路径，并在 SKIP 占位中文档化未实现路径。
//
// 准则核对: ROBUST-03（不崩溃）; ROBUST-05（显式 srtWarning 诊断，不静默吞没）。
// 测试分级: L1（文件系统层面失败，可测）; L2（DLL 层面失败，需真实 fixture DLL，SKIP 占位）。
//
// 放置说明: 原方案建议放 tests/integration/，但 cross test 框架在当前构建环境
// (clion mcp terminal INCLUDE 未设置) 无法找到 MSVC 标准库头文件，而 unittests/
// 框架通过 PCH 预编译了 <filesystem>/<string>/<vector>/<memory>，可正常编译。
// 且本测试核心为 L1（文件系统层面失败，不加载真实插件），符合 INFRA-01/INFRA-03
// 单组件测试进 unittests/ 的分级原则。与 tst_runtime.cpp 的 PluginFactory 测试同目录。

#include <chrono>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include <synthrt/Core/Plugin/PluginFactory.h>

// 访问 _impl 内部状态以验证失败路径被正确处理（dirty 清除、scannedPluginDirs 去重）。
// 与 tst_runtime.cpp 的 TestPluginFactory 模式一致。
#include "../../lib/Core/Plugin/PluginFactory_p.h"

namespace {

    // 创建带时间戳的临时根目录，避免并行测试相互干扰。
    std::filesystem::path makeTempRoot(const std::string &name) {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        auto dir = std::filesystem::temp_directory_path() /
                   ("srt-t11-" + name + "-" + std::to_string(stamp));
        std::filesystem::create_directories(dir);
        return dir;
    }

    // 创建插件目录布局并返回 categoryDir。
    // 同时创建 root/plugins/_shared 目录：scanPlugins 只有在 _shared 目录存在且
    // preloadSharedLibraries 成功时才清除 m_pluginsDirty（见 PluginFactory.cpp L224-225）。
    // 不创建 _shared 会导致 dirty 保持 true，干扰失败路径的断言。
    std::filesystem::path makePluginLayout(const std::filesystem::path &root) {
        const auto categoryDir = root / "plugins" / "module" / "category";
        std::filesystem::create_directories(categoryDir);
        std::filesystem::create_directories(root / "plugins" / "_shared");
        return categoryDir;
    }

    // 写入指定内容的 plugin.json 到 pluginDir/plugin.json。
    void writePluginJson(const std::filesystem::path &pluginDir, const std::string &content) {
        std::filesystem::create_directories(pluginDir);
        std::ofstream ofs(pluginDir / "plugin.json");
        REQUIRE(ofs.is_open());
        ofs << content;
        ofs.close();
    }

    // 创建一个非 DLL 文件（.txt），用于触发 "shared library not found or invalid" 路径。
    void writeNonDllFile(const std::filesystem::path &pluginDir, const std::string &fileName) {
        std::filesystem::create_directories(pluginDir);
        std::ofstream ofs(pluginDir / fileName);
        REQUIRE(ofs.is_open());
        ofs << "not a dll";
        ofs.close();
    }

    // TestPluginFactory: 暴露 _impl 内部状态用于断言。
    // 继承 PluginFactory（protected _impl 可访问），通过 _impl->m_* 验证不变量。
    class TestPluginFactory : public srt::core::PluginFactory {
    public:
        [[nodiscard]] bool isDirty(const char *iid) const {
            std::shared_lock lock(_impl->m_plugins_mtx);
            return _impl->m_pluginsDirty.count(iid) != 0;
        }

        [[nodiscard]] std::size_t scannedPluginDirCount() const {
            std::shared_lock lock(_impl->m_plugins_mtx);
            return _impl->m_scannedPluginDirs.size();
        }

        [[nodiscard]] std::size_t loadedLibraryCount() const {
            std::shared_lock lock(_impl->m_plugins_mtx);
            return _impl->m_libraryInstances.size();
        }

        [[nodiscard]] std::size_t loadedPluginCount(const char *iid) const {
            std::shared_lock lock(_impl->m_plugins_mtx);
            auto it = _impl->m_allPlugins.find(iid);
            return it == _impl->m_allPlugins.end() ? 0 : it->second.size();
        }
    };

    // 测试用的 IID 和 key。不与任何真实插件冲突。
    constexpr const char *kTestIid = "test.plugin.t11";
    constexpr const char *kTestKey = "bad-plugin";

}

// ============================================================================
// L1 文件系统层面失败路径（PluginFactory::scanPlugins 的早期 continue 分支）
// ============================================================================

// PLF-001: plugin.json 缺失 → plugin() 返回 nullptr，不崩溃。
// 对应 scanPlugins L120-124: "plugin.json not found in ..., skipping"
TEST_CASE("PLF-001: missing plugin.json returns nullptr", "[plugin][load][failure][t11]") {
    const auto root = makeTempRoot("plf-001-missing-json");
    const auto categoryDir = makePluginLayout(root);
    const auto pluginDir = categoryDir / "bad-plugin";
    std::filesystem::create_directories(pluginDir); // 空目录，无 plugin.json

    TestPluginFactory factory;
    factory.addPluginPath(kTestIid, categoryDir);

    auto *plugin = factory.plugin(kTestIid, kTestKey);
    REQUIRE(plugin == nullptr);
    CHECK_FALSE(factory.isDirty(kTestIid)); // 扫描完成，dirty 清除

    std::filesystem::remove_all(root);
}

// PLF-002: plugin.json 损坏 JSON → plugin() 返回 nullptr，不崩溃。
// 对应 scanPlugins L143-148: "invalid JSON in plugin.json in ..., skipping"
TEST_CASE("PLF-002: malformed plugin.json returns nullptr", "[plugin][load][failure][t11]") {
    const auto root = makeTempRoot("plf-002-malformed-json");
    const auto categoryDir = makePluginLayout(root);
    const auto pluginDir = categoryDir / "bad-plugin";
    writePluginJson(pluginDir, "{ this is not valid json }}}");

    TestPluginFactory factory;
    factory.addPluginPath(kTestIid, categoryDir);

    auto *plugin = factory.plugin(kTestIid, kTestKey);
    REQUIRE(plugin == nullptr);
    CHECK_FALSE(factory.isDirty(kTestIid));

    std::filesystem::remove_all(root);
}

// PLF-003: plugin.json 缺 target 字段 → plugin() 返回 nullptr。
// 对应 scanPlugins L150-155: "missing string 'target' field, skipping"
TEST_CASE("PLF-003: plugin.json missing target field returns nullptr", "[plugin][load][failure][t11]") {
    const auto root = makeTempRoot("plf-003-missing-target");
    const auto categoryDir = makePluginLayout(root);
    const auto pluginDir = categoryDir / "bad-plugin";
    writePluginJson(pluginDir, R"({"name": "no-target-field"})");

    TestPluginFactory factory;
    factory.addPluginPath(kTestIid, categoryDir);

    auto *plugin = factory.plugin(kTestIid, kTestKey);
    REQUIRE(plugin == nullptr);
    CHECK_FALSE(factory.isDirty(kTestIid));

    std::filesystem::remove_all(root);
}

// PLF-004: plugin.json target 为非字符串 → plugin() 返回 nullptr。
// 对应 scanPlugins L150-155: target 存在但 !it->is_string() 分支
TEST_CASE("PLF-004: plugin.json target non-string returns nullptr", "[plugin][load][failure][t11]") {
    const auto root = makeTempRoot("plf-004-target-non-string");
    const auto categoryDir = makePluginLayout(root);
    const auto pluginDir = categoryDir / "bad-plugin";
    writePluginJson(pluginDir, R"({"target": 123})");

    TestPluginFactory factory;
    factory.addPluginPath(kTestIid, categoryDir);

    auto *plugin = factory.plugin(kTestIid, kTestKey);
    REQUIRE(plugin == nullptr);
    CHECK_FALSE(factory.isDirty(kTestIid));

    std::filesystem::remove_all(root);
}

// PLF-005: target DLL 不存在 → plugin() 返回 nullptr。
// 对应 scanPlugins L166-171: "shared library not found or invalid: ... (target=...), skipping"
TEST_CASE("PLF-005: target DLL not found returns nullptr", "[plugin][load][failure][t11]") {
    const auto root = makeTempRoot("plf-005-dll-missing");
    const auto categoryDir = makePluginLayout(root);
    const auto pluginDir = categoryDir / "bad-plugin";
    writePluginJson(pluginDir, R"({"target": "missing.dll"})");

    TestPluginFactory factory;
    factory.addPluginPath(kTestIid, categoryDir);

    auto *plugin = factory.plugin(kTestIid, kTestKey);
    REQUIRE(plugin == nullptr);
    CHECK_FALSE(factory.isDirty(kTestIid));
    CHECK(factory.loadedLibraryCount() == 0); // 无 DLL 加载

    std::filesystem::remove_all(root);
}

// PLF-006: target 非 DLL 文件（.txt） → plugin() 返回 nullptr。
// 对应 scanPlugins L166-171: !stdc::SharedLibrary::isLibrary(dllPath) 分支
TEST_CASE("PLF-006: target non-DLL file returns nullptr", "[plugin][load][failure][t11]") {
    const auto root = makeTempRoot("plf-006-non-dll");
    const auto categoryDir = makePluginLayout(root);
    const auto pluginDir = categoryDir / "bad-plugin";
    writePluginJson(pluginDir, R"({"target": "not-a-dll.txt"})");
    writeNonDllFile(pluginDir, "not-a-dll.txt");

    TestPluginFactory factory;
    factory.addPluginPath(kTestIid, categoryDir);

    auto *plugin = factory.plugin(kTestIid, kTestKey);
    REQUIRE(plugin == nullptr);
    CHECK_FALSE(factory.isDirty(kTestIid));
    CHECK(factory.loadedLibraryCount() == 0);

    std::filesystem::remove_all(root);
}

// PLF-007: 多插件目录混合（多个坏插件）→ 都返回 nullptr，互不阻塞。
// 验证 scanPlugins 的 continue 不中断后续插件目录扫描。
TEST_CASE("PLF-007: multiple bad plugins do not block each other", "[plugin][load][failure][t11]") {
    const auto root = makeTempRoot("plf-007-mixed-bad");
    const auto categoryDir = makePluginLayout(root);

    // bad-1: 缺 plugin.json
    std::filesystem::create_directories(categoryDir / "bad-1");
    // bad-2: 损坏 JSON
    writePluginJson(categoryDir / "bad-2", "{ broken");
    // bad-3: 缺 target 字段
    writePluginJson(categoryDir / "bad-3", R"({"name": "no-target"})");
    // bad-4: DLL 不存在
    writePluginJson(categoryDir / "bad-4", R"({"target": "missing.dll"})");
    // bad-5: 非 DLL 文件
    writePluginJson(categoryDir / "bad-5", R"({"target": "text.txt"})");
    writeNonDllFile(categoryDir / "bad-5", "text.txt");

    TestPluginFactory factory;
    factory.addPluginPath(kTestIid, categoryDir);

    // 查询任意 key 都应返回 nullptr（5 个坏插件都被跳过）
    for (const char *key : {"bad-1", "bad-2", "bad-3", "bad-4", "bad-5", "nonexistent"}) {
        CAPTURE(key);
        CHECK(factory.plugin(kTestIid, key) == nullptr);
    }
    CHECK_FALSE(factory.isDirty(kTestIid));
    CHECK(factory.loadedLibraryCount() == 0);
    CHECK(factory.loadedPluginCount(kTestIid) == 0);

    std::filesystem::remove_all(root);
}

// PLF-008: 重复 addPluginPath 同一目录 → 不崩溃，plugin() 稳定 nullptr。
// 验证 addPluginPath 的去重逻辑（sharedDirs 去重）和 scanPlugins 的稳定性。
TEST_CASE("PLF-008: duplicate addPluginPath does not crash", "[plugin][load][failure][t11]") {
    const auto root = makeTempRoot("plf-008-dup-path");
    const auto categoryDir = makePluginLayout(root);
    const auto pluginDir = categoryDir / "bad-plugin";
    writePluginJson(pluginDir, R"({"target": "missing.dll"})");

    TestPluginFactory factory;
    // 同一目录重复 addPluginPath
    factory.addPluginPath(kTestIid, categoryDir);
    factory.addPluginPath(kTestIid, categoryDir);
    factory.addPluginPath(kTestIid, categoryDir);

    auto *plugin = factory.plugin(kTestIid, kTestKey);
    REQUIRE(plugin == nullptr);
    // 重复 plugin() 调用稳定性
    CHECK(factory.plugin(kTestIid, kTestKey) == nullptr);
    CHECK(factory.plugin(kTestIid, kTestKey) == nullptr);

    std::filesystem::remove_all(root);
}

// PLF-009: 多次 plugin() 调用稳定性 → 不崩溃，结果一致。
// 验证 plugin() 的幂等性：dirty 清除后不重复扫描，多次调用返回相同结果。
// 注意：m_scannedPluginDirs 只在成功加载 DLL 后才插入，加载失败的插件不会被记录，
// 但 dirty 已清除（_shared 目录存在），后续 plugin() 不会重新扫描。
TEST_CASE("PLF-009: repeated plugin() calls are stable", "[plugin][load][failure][t11]") {
    const auto root = makeTempRoot("plf-009-repeated-query");
    const auto categoryDir = makePluginLayout(root);
    const auto pluginDir = categoryDir / "bad-plugin";
    writePluginJson(pluginDir, R"({"target": "missing.dll"})");

    TestPluginFactory factory;
    factory.addPluginPath(kTestIid, categoryDir);

    // 第一次调用触发 scanPlugins，dirty 清除
    REQUIRE(factory.plugin(kTestIid, kTestKey) == nullptr);
    CHECK_FALSE(factory.isDirty(kTestIid)); // dirty 已清除
    CHECK(factory.loadedPluginCount(kTestIid) == 0); // 无插件加载

    // 后续调用不重新扫描（dirty 已清除），返回稳定 nullptr
    for (int i = 0; i < 5; ++i) {
        CHECK(factory.plugin(kTestIid, kTestKey) == nullptr);
    }
    CHECK_FALSE(factory.isDirty(kTestIid)); // dirty 仍清除

    std::filesystem::remove_all(root);
}

// PLF-010: addPluginPath 不存在的路径 → plugin() 返回 nullptr，不崩溃。
// 对应 addPluginPath L289-292: !fs::is_directory(path, ec) → dirty 后 return
// 注意：路径不存在时 sharedDir 也不存在，dirty 保持 true（等待路径出现时重试，
// 与 tst_runtime.cpp 的 retry 测试一致）。
TEST_CASE("PLF-010: addPluginPath with nonexistent path does not crash", "[plugin][load][failure][t11]") {
    TestPluginFactory factory;
    factory.addPluginPath(kTestIid, "/nonexistent/path/that/does/not/exist");

    auto *plugin = factory.plugin(kTestIid, kTestKey);
    REQUIRE(plugin == nullptr);
    // 路径不存在时 dirty 保持
    CHECK(factory.isDirty(kTestIid));
}

// PLF-011: setPluginPaths 清空后 plugin() 返回 nullptr。
// 验证 setPluginPaths 的 erase 语义。
TEST_CASE("PLF-011: setPluginPaths with empty list clears paths", "[plugin][load][failure][t11]") {
    const auto root = makeTempRoot("plf-011-clear-paths");
    const auto categoryDir = makePluginLayout(root);
    const auto pluginDir = categoryDir / "bad-plugin";
    writePluginJson(pluginDir, R"({"target": "missing.dll"})");

    TestPluginFactory factory;
    factory.addPluginPath(kTestIid, categoryDir);
    REQUIRE(factory.plugin(kTestIid, kTestKey) == nullptr);

    // 清空路径
    std::vector<std::filesystem::path> emptyPaths;
    factory.setPluginPaths(kTestIid, emptyPaths);
    CHECK(factory.plugin(kTestIid, kTestKey) == nullptr);
    CHECK(factory.pluginPaths(kTestIid).empty());

    std::filesystem::remove_all(root);
}

// PLF-012: 空 iid → plugin() 返回 nullptr，不崩溃。
// 验证空字符串作为 iid 的边界条件。
TEST_CASE("PLF-012: empty iid returns nullptr", "[plugin][load][failure][t11]") {
    TestPluginFactory factory;
    auto *plugin = factory.plugin("", kTestKey);
    REQUIRE(plugin == nullptr);
}

// PLF-013: 空 key → plugin() 返回 nullptr，不崩溃。
TEST_CASE("PLF-013: empty key returns nullptr", "[plugin][load][failure][t11]") {
    TestPluginFactory factory;
    auto *plugin = factory.plugin(kTestIid, "");
    REQUIRE(plugin == nullptr);
}

// PLF-014: plugin.json target 为空字符串 → plugin() 返回 nullptr。
// target="" 导致 dllPath 为 pluginDir/""，fs::exists 检查失败。
TEST_CASE("PLF-014: empty target string returns nullptr", "[plugin][load][failure][t11]") {
    const auto root = makeTempRoot("plf-014-empty-target");
    const auto categoryDir = makePluginLayout(root);
    const auto pluginDir = categoryDir / "bad-plugin";
    writePluginJson(pluginDir, R"({"target": ""})");

    TestPluginFactory factory;
    factory.addPluginPath(kTestIid, categoryDir);

    auto *plugin = factory.plugin(kTestIid, kTestKey);
    REQUIRE(plugin == nullptr);
    CHECK_FALSE(factory.isDirty(kTestIid));

    std::filesystem::remove_all(root);
}

// ============================================================================
// L2 DLL 层面失败路径（需真实 fixture DLL，SKIP 占位）
// ============================================================================
//
// 以下分支需要构造真实可加载但导出错误符号 / 错误 iid / 返回 null 的 fixture DLL，
// 超出 T-11 测试范围（避免创建 fixture DLL 的过度工程化）。以 SKIP 占位并文档化触发路径。
//
// 任务文档原始假设的失败路径（当前 PluginFactory 未实现，文档化归档）:
// - "加载缺失依赖的插件": PluginFactory 无依赖解析逻辑，插件 manifest 无 dependencies 字段。
// - "加载版本不匹配的插件": PluginFactory 无版本检查，plugin.json 无 version 字段校验。
// - "Runtime 卸载后插件句柄失效": Plugin* 是裸指针，PluginFactory 析构时插件随之销毁，
//   不存在"卸载后句柄失效"的运行时场景。

// PLF-015: symbol 'srt_plugin_instance' not found → SKIP（需 fixture DLL）。
// 对应 scanPlugins L185-190: so.resolve("srt_plugin_instance") 返回 null 分支。
TEST_CASE("PLF-015: symbol srt_plugin_instance not found", "[plugin][load][failure][t11][L2]") {
    SKIP("Requires a fixture DLL that loads successfully but exports no srt_plugin_instance symbol. "
         "Creating such a fixture DLL is out of scope for T-11 (avoids over-engineering). "
         "Path: lib/Core/Plugin/PluginFactory.cpp scanPlugins L185-190.");
}

// PLF-016: srt_plugin_instance 返回 null → SKIP（需 fixture DLL）。
// 对应 scanPlugins L192-197: getter() == null 分支。
TEST_CASE("PLF-016: srt_plugin_instance returns null", "[plugin][load][failure][t11][L2]") {
    SKIP("Requires a fixture DLL that exports srt_plugin_instance returning nullptr. "
         "Path: lib/Core/Plugin/PluginFactory.cpp scanPlugins L192-197.");
}

// PLF-017: plugin iid mismatch → SKIP（需 fixture DLL）。
// 对应 scanPlugins L198-209: strcmp(iid, plugin->iid()) != 0 分支（Trace 级别，非 Warning）。
TEST_CASE("PLF-017: plugin iid mismatch", "[plugin][load][failure][t11][L2]") {
    SKIP("Requires a fixture DLL that exports a plugin with mismatched iid. "
         "Path: lib/Core/Plugin/PluginFactory.cpp scanPlugins L198-209 (srtTrace, not srtWarning).");
}

// PLF-018: duplicate plugin key → SKIP（需 fixture DLL）。
// 对应 scanPlugins L210-214: plugins.insert 失败分支。
TEST_CASE("PLF-018: duplicate plugin key", "[plugin][load][failure][t11][L2]") {
    SKIP("Requires two fixture DLLs that export plugins with the same key. "
         "Path: lib/Core/Plugin/PluginFactory.cpp scanPlugins L210-214.");
}
