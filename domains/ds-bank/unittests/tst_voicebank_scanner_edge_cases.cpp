// VoicebankScanner 边缘场景与极端情况测试。
//
// 覆盖任务要求中明确列出的极端场景：
//   - 版本号 v 前缀处理（"v1.0" 与 "1.0" 的等价性）
//   - 5+ 段版本号（VersionNumber 截断/解析行为）
//   - 自依赖（包的 dependencies 列表中包含自己）
//   - 2 节点循环依赖（A 依赖 B，B 依赖 A）
//   - 3 节点循环依赖（A->B->C->A）
//   - Diamond 依赖（A->B, A->C, B->D, C->D）
//   - 缺失依赖（dependencies 引用不存在的 packageId）
//   - Unicode 路径包目录扫描
//   - 增量热更新：首次扫描后新增包，二次扫描应发现两个
//   - 增量热更新：替换包内容，二次扫描应反映新内容
//
// 重要说明（设计原则 ROBUST-05）：
//   VoicebankScanner 本身不做依赖解析、循环检测或缺失依赖填充。
//   dependencies 字段仅原样存储从 desc.json 解析得到的字符串列表；
//   unresolvedDependencies 字段在 VoicebankScanner 中不会被填充（保持空）。
//   依赖解析与循环检测是 Runtime / SynthrtEngine 的职责（见 ARCH-06）。
//   因此本文件验证：
//     1. VoicebankScanner 在面对循环/自依赖/diamond 时不崩溃、不无限递归
//     2. dependencies 列表被原样保留，便于上层做解析
//     3. unresolvedDependencies 保持空（VoicebankScanner 不负责填充）
//
// 设计原则对应：
//   ROBUST-01: refresh() 返回 Expected，不抛异常
//   ROBUST-02: 第三方（文件系统/JSON）异常在边界转换为 Error
//   INFRA-03: L1 单组件测试不加载插件 DLL
//   CODING-03: 路径操作避免直接 path.string()（测试中仅用于断言比较）

#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <stdcorelib/support/versionnumber.h>

#include <diffsinger/Bank/VoicebankScanner.h>

using namespace ds::bank;

namespace {

    // RAII 临时目录：构造时创建，析构时清理。避免测试间状态泄漏。
    struct TempDir {
        std::filesystem::path path;
        TempDir(const std::string &name) {
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            path = std::filesystem::temp_directory_path() /
                  ("ds-bank-edge-" + name + "-" + std::to_string(stamp));
            std::filesystem::create_directories(path);
        }
        ~TempDir() {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
        TempDir(const TempDir &) = delete;
        TempDir &operator=(const TempDir &) = delete;
    };

    void writeFile(const std::filesystem::path &path, const std::string &text) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path);
        file << text;
    }

    // 创建一个最小可解析的 voicebank 包。
    // 可选地传入 dependencies 列表，写入 desc.json 的 dependencies 字段。
    void createPackageWithDeps(const std::filesystem::path &pkgDir,
                               const std::string &packageId,
                               const std::string &version,
                               const std::vector<std::string> &dependencies = {},
                               const std::string &singerId = "test_singer") {
        std::string desc = "{\n";
        desc += "    \"id\": \"" + packageId + "\",\n";
        desc += "    \"version\": \"" + version + "\",\n";
        if (!dependencies.empty()) {
            desc += "    \"dependencies\": [";
            for (size_t i = 0; i < dependencies.size(); ++i) {
                if (i > 0) desc += ", ";
                desc += "\"" + dependencies[i] + "\"";
            }
            desc += "],\n";
        }
        desc += "    \"contributes\": {\n";
        desc += "        \"singers\": [\"characters/singer/config.json\"],\n";
        desc += "        \"inferences\": [\"inferences/duration/config.json\"]\n";
        desc += "    }\n";
        desc += "}\n";
        writeFile(pkgDir / "desc.json", desc);

        std::string singer = "{\n";
        singer += "    \"$version\": \"1.0\",\n";
        singer += "    \"id\": \"" + singerId + "\",\n";
        singer += "    \"level\": 1,\n";
        singer += "    \"imports\": [{\"inferenceId\": \"duration\"}],\n";
        singer += "    \"configuration\": {\n";
        singer += "        \"defaultLanguage\": \"cmn\",\n";
        singer += "        \"languages\": [{\"id\": \"cmn\", \"g2p\": \"g2p-cmn-official\", \"s2pMode\": \"dict\"}]\n";
        singer += "    }\n";
        singer += "}\n";
        writeFile(pkgDir / "characters/singer/config.json", singer);

        std::string inference = "{\n";
        inference += "    \"id\": \"duration\",\n";
        inference += "    \"class\": \"ai.svs.DurationInference\",\n";
        inference += "    \"level\": 1,\n";
        inference += "    \"configuration\": {}\n";
        inference += "}\n";
        writeFile(pkgDir / "inferences/duration/config.json", inference);
    }

    // 在指定根目录下创建一个简单的可解析包（无 dependencies）。
    void createPackage(const std::filesystem::path &pkgDir,
                       const std::string &packageId,
                       const std::string &version,
                       const std::string &singerId = "test_singer") {
        createPackageWithDeps(pkgDir, packageId, version, {}, singerId);
    }

    // 在 PackageStatus 列表中查找指定 packageId 的状态条目。
    const PackageStatus *findStatus(const std::vector<PackageStatus> &statuses,
                                    const std::string &packageId) {
        for (const auto &s : statuses) {
            if (s.packageId == packageId) {
                return &s;
            }
        }
        return nullptr;
    }

} // namespace

// ===========================================================================
// 版本号 v 前缀处理：验证 versionsMatch 对 "v1.0" 与 "1.0" 的行为
//
// VoicebankScanner::versionsMatch 内部使用 stdc::VersionNumber::fromString。
// 若 fromString 不识别 'v' 前缀，则会回退到字符串比较，"v1.0" != "1.0"。
// 此测试记录实际行为（不修改源码），便于评估是否需要在前端规范化。
// ===========================================================================

TEST_CASE("VoicebankScanner v-prefix version query behavior",
          "[ds-bank][edge][version][v-prefix]") {
    TempDir dir("v-prefix");
    // 包以 "1.0.0" 存储
    createPackage(dir.path / "pkg", "pkg.vprefix", "1.0.0");

    VoicebankScanner scanner;
    scanner.setSearchPaths({dir.path});
    auto exp = scanner.refresh();
    REQUIRE(exp.hasValue());
    REQUIRE(scanner.singers().size() == 1);
    REQUIRE(scanner.singers()[0].ref.version == "1.0");

    // 用 "v1.0.0" 查询：记录实际行为（匹配或不匹配）。
    // 注意：VersionNumber 可能不识别 'v' 前缀，导致 versionsMatch 回退到
    // 字符串比较 "1.0" == "v1.0.0" -> false。这是潜在的使用陷阱，
    // 调用方应避免在查询时使用 'v' 前缀，或在调用前剥离前缀。
    SingerRef refV{"pkg.vprefix", "test_singer", "v1.0.0"};
    auto snapV = scanner.singerSnapshot(refV);
    // 不强制要求匹配——仅记录行为。若 behavior 改变（例如未来 VersionNumber
    // 支持 'v' 前缀），此断言可同步更新。
    bool matches = snapV.hasValue();
    INFO("v-prefix query 'v1.0.0' against stored '1.0': matches=" << matches);
    // 当前预期：不匹配（VersionNumber 不识别 'v' 前缀）
    CHECK(!matches);

    // 用 "1.0" 查询：必须匹配（语义等价）
    SingerRef refPlain{"pkg.vprefix", "test_singer", "1.0"};
    auto snapPlain = scanner.singerSnapshot(refPlain);
    REQUIRE(snapPlain.hasValue());
}

TEST_CASE("VoicebankScanner v-prefix in stored version behaves consistently",
          "[ds-bank][edge][version][v-prefix]") {
    TempDir dir("v-stored");
    // 包的 desc.json 中 version 字段写 "v1.0.0"（不规范写法）
    createPackage(dir.path / "pkg", "pkg.vstored", "v1.0.0");

    VoicebankScanner scanner;
    scanner.setSearchPaths({dir.path});
    auto exp = scanner.refresh();
    REQUIRE(exp.hasValue());
    REQUIRE(scanner.singers().size() == 1);

    // 记录 VersionNumber 解析 "v1.0.0" 后的 toString() 结果。
    // 这反映了 VoicebankScanner 存储的版本字符串形式。
    const auto &stored = scanner.singers()[0].ref.version;
    INFO("stored version after parsing 'v1.0.0': '" << stored << "'");

    // 无论 stored 是 "v1.0" 还是 "1.0"，用相同字符串查询必须匹配
    SingerRef refSame{"pkg.vstored", "test_singer", stored};
    auto snapSame = scanner.singerSnapshot(refSame);
    REQUIRE(snapSame.hasValue());

    // 用空 version 查询：必须匹配（empty 表示无过滤）
    SingerRef refEmpty{"pkg.vstored", "test_singer", ""};
    auto snapEmpty = scanner.singerSnapshot(refEmpty);
    REQUIRE(snapEmpty.hasValue());
}

// ===========================================================================
// 5+ 段版本号：VersionNumber 通常支持 4 段（major.minor.patch.build）
// 此测试验证 5 段版本号 "1.2.3.4.5" 的解析与匹配行为
// ===========================================================================

TEST_CASE("VoicebankScanner 5-segment version parses without crash",
          "[ds-bank][edge][version][segments]") {
    TempDir dir("five-seg");
    createPackage(dir.path / "pkg", "pkg.fiveseg", "1.2.3.4.5");

    VoicebankScanner scanner;
    scanner.setSearchPaths({dir.path});
    auto exp = scanner.refresh();
    REQUIRE(exp.hasValue());
    REQUIRE(scanner.singers().size() == 1);

    // 不论 VersionNumber 如何截断/解析 5 段，存储的 version 字符串
    // 应该是某个规范化形式。记录实际值。
    const auto &stored = scanner.singers()[0].ref.version;
    INFO("5-segment '1.2.3.4.5' parsed/stored as: '" << stored << "'");
    // 至少不应为空（解析失败时应保留某种形式）
    REQUIRE(!stored.empty());

    // 用存储值查询必须匹配
    SingerRef refStored{"pkg.fiveseg", "test_singer", stored};
    REQUIRE(scanner.singerSnapshot(refStored).hasValue());

    // 用空 version 查询必须匹配
    SingerRef refEmpty{"pkg.fiveseg", "test_singer", ""};
    REQUIRE(scanner.singerSnapshot(refEmpty).hasValue());
}

TEST_CASE("VoicebankScanner 4-segment version round-trip matches",
          "[ds-bank][edge][version][segments]") {
    TempDir dir("four-seg");
    createPackage(dir.path / "pkg", "pkg.fourseg", "1.2.3.4");

    VoicebankScanner scanner;
    scanner.setSearchPaths({dir.path});
    scanner.refresh();
    REQUIRE(scanner.singers().size() == 1);

    // 4 段版本号：1.2.3.4 应保留 patch+build 信息
    const auto &stored = scanner.singers()[0].ref.version;
    INFO("4-segment '1.2.3.4' stored as: '" << stored << "'");

    // 等价形式查询（参考 BF-21）
    SingerRef ref1{"pkg.fourseg", "test_singer", "1.2.3.4"};
    REQUIRE(scanner.singerSnapshot(ref1).hasValue());
    SingerRef ref2{"pkg.fourseg", "test_singer", "1.2.3.4.0"};
    REQUIRE(scanner.singerSnapshot(ref2).hasValue());
}

// ===========================================================================
// 自依赖：包的 dependencies 列表中包含自己
//
// VoicebankScanner 不做依赖解析，因此自依赖不应导致崩溃或无限循环。
// dependencies 字段原样存储，由上层（Runtime）做循环检测。
// ===========================================================================

TEST_CASE("VoicebankScanner self-dependency does not crash",
          "[ds-bank][edge][deps][self-cycle]") {
    TempDir dir("self-dep");
    // pkg.self 依赖自身
    createPackageWithDeps(dir.path / "pkg", "pkg.self", "1.0.0",
                          {"pkg.self"});

    VoicebankScanner scanner;
    scanner.setSearchPaths({dir.path});
    auto exp = scanner.refresh();
    REQUIRE(exp.hasValue());
    REQUIRE(scanner.singers().size() == 1);

    // 验证 dependencies 原样存储（不丢、不去重、不解析）
    const auto *status = findStatus(exp.value(), "pkg.self");
    REQUIRE(status != nullptr);
    REQUIRE(status->valid);
    REQUIRE(status->dependencies.size() == 1);
    REQUIRE(status->dependencies[0] == "pkg.self");

    // VoicebankScanner 不填充 unresolvedDependencies（那是上层的职责）
    REQUIRE(status->unresolvedDependencies.empty());
}

// ===========================================================================
// 2 节点循环依赖：A 依赖 B，B 依赖 A
// ===========================================================================

TEST_CASE("VoicebankScanner 2-node cycle does not crash",
          "[ds-bank][edge][deps][cycle]") {
    TempDir dir("cycle-2");
    createPackageWithDeps(dir.path / "pkg_a", "pkg.a", "1.0.0", {"pkg.b"});
    createPackageWithDeps(dir.path / "pkg_b", "pkg.b", "1.0.0", {"pkg.a"});

    VoicebankScanner scanner;
    scanner.setSearchPaths({dir.path});
    auto exp = scanner.refresh();
    REQUIRE(exp.hasValue());
    REQUIRE(scanner.singers().size() == 2);

    // 两个包都应成功解析，dependencies 原样存储
    const auto *statusA = findStatus(exp.value(), "pkg.a");
    REQUIRE(statusA != nullptr);
    REQUIRE(statusA->valid);
    REQUIRE(statusA->dependencies.size() == 1);
    REQUIRE(statusA->dependencies[0] == "pkg.b");

    const auto *statusB = findStatus(exp.value(), "pkg.b");
    REQUIRE(statusB != nullptr);
    REQUIRE(statusB->valid);
    REQUIRE(statusB->dependencies.size() == 1);
    REQUIRE(statusB->dependencies[0] == "pkg.a");
}

// ===========================================================================
// 3 节点循环依赖：A->B->C->A
// ===========================================================================

TEST_CASE("VoicebankScanner 3-node cycle does not crash",
          "[ds-bank][edge][deps][cycle]") {
    TempDir dir("cycle-3");
    createPackageWithDeps(dir.path / "pkg_a", "pkg.a", "1.0.0", {"pkg.b"});
    createPackageWithDeps(dir.path / "pkg_b", "pkg.b", "1.0.0", {"pkg.c"});
    createPackageWithDeps(dir.path / "pkg_c", "pkg.c", "1.0.0", {"pkg.a"});

    VoicebankScanner scanner;
    scanner.setSearchPaths({dir.path});
    auto exp = scanner.refresh();
    REQUIRE(exp.hasValue());
    REQUIRE(scanner.singers().size() == 3);

    // 验证三个包都成功解析，dependencies 链路完整
    REQUIRE(findStatus(exp.value(), "pkg.a")->dependencies[0] == "pkg.b");
    REQUIRE(findStatus(exp.value(), "pkg.b")->dependencies[0] == "pkg.c");
    REQUIRE(findStatus(exp.value(), "pkg.c")->dependencies[0] == "pkg.a");
}

// ===========================================================================
// Diamond 依赖：A->B, A->C, B->D, C->D（D 被两条路径依赖）
// ===========================================================================

TEST_CASE("VoicebankScanner diamond dependency preserves all edges",
          "[ds-bank][edge][deps][diamond]") {
    TempDir dir("diamond");
    createPackageWithDeps(dir.path / "pkg_a", "pkg.a", "1.0.0",
                          {"pkg.b", "pkg.c"});
    createPackageWithDeps(dir.path / "pkg_b", "pkg.b", "1.0.0", {"pkg.d"});
    createPackageWithDeps(dir.path / "pkg_c", "pkg.c", "1.0.0", {"pkg.d"});
    createPackageWithDeps(dir.path / "pkg_d", "pkg.d", "1.0.0", {});

    VoicebankScanner scanner;
    scanner.setSearchPaths({dir.path});
    auto exp = scanner.refresh();
    REQUIRE(exp.hasValue());
    REQUIRE(scanner.singers().size() == 4);

    // A 依赖 [B, C]
    const auto *statusA = findStatus(exp.value(), "pkg.a");
    REQUIRE(statusA != nullptr);
    REQUIRE(statusA->dependencies.size() == 2);
    REQUIRE(statusA->dependencies[0] == "pkg.b");
    REQUIRE(statusA->dependencies[1] == "pkg.c");

    // B 和 C 都依赖 D（diamond 汇聚点）
    REQUIRE(findStatus(exp.value(), "pkg.b")->dependencies ==
            std::vector<std::string>{"pkg.d"});
    REQUIRE(findStatus(exp.value(), "pkg.c")->dependencies ==
            std::vector<std::string>{"pkg.d"});

    // D 无依赖
    REQUIRE(findStatus(exp.value(), "pkg.d")->dependencies.empty());
}

// ===========================================================================
// 缺失依赖：dependencies 引用不存在的 packageId
//
// VoicebankScanner 不解析依赖，因此缺失依赖不会导致 status.valid=false。
// unresolvedDependencies 字段保持空（由上层 Runtime 填充）。
// ===========================================================================

TEST_CASE("VoicebankScanner missing dependency does not invalidate package",
          "[ds-bank][edge][deps][missing]") {
    TempDir dir("missing-dep");
    createPackageWithDeps(dir.path / "pkg", "pkg.missingdep", "1.0.0",
                          {"pkg.nonexistent"});

    VoicebankScanner scanner;
    scanner.setSearchPaths({dir.path});
    auto exp = scanner.refresh();
    REQUIRE(exp.hasValue());
    REQUIRE(scanner.singers().size() == 1);

    const auto *status = findStatus(exp.value(), "pkg.missingdep");
    REQUIRE(status != nullptr);
    // 包本身仍然有效（manifest 解析成功）
    REQUIRE(status->valid);
    // dependencies 原样保留
    REQUIRE(status->dependencies.size() == 1);
    REQUIRE(status->dependencies[0] == "pkg.nonexistent");
    // unresolvedDependencies 由上层填充，VoicebankScanner 不负责
    REQUIRE(status->unresolvedDependencies.empty());
}

// ===========================================================================
// Unicode 路径包目录扫描：包目录名包含中文/日文
// ===========================================================================

TEST_CASE("VoicebankScanner unicode package directory name is scanned",
          "[ds-bank][edge][unicode][path]") {
    TempDir dir("unicode-dir");
    // 包目录名使用中文
    createPackage(dir.path / "声库包_中文", "pkg.unicode", "1.0.0");

    VoicebankScanner scanner;
    scanner.setSearchPaths({dir.path});
    auto exp = scanner.refresh();
    REQUIRE(exp.hasValue());
    REQUIRE(scanner.singers().size() == 1);
    REQUIRE(scanner.singers()[0].ref.packageId == "pkg.unicode");

    // packageDirectory 应能找到 Unicode 路径
    auto dirs = scanner.packageDirectories("pkg.unicode");
    REQUIRE(dirs.size() == 1);
    REQUIRE(!dirs[0].path.empty());

    std::filesystem::remove_all(dir.path);
}

TEST_CASE("VoicebankScanner unicode packageId is preserved",
          "[ds-bank][edge][unicode][id]") {
    TempDir dir("unicode-id");
    // packageId 使用中文/日文混合
    const std::string packageId = "歌手.日本語";
    createPackage(dir.path / "pkg", packageId, "1.0.0");

    VoicebankScanner scanner;
    scanner.setSearchPaths({dir.path});
    auto exp = scanner.refresh();
    REQUIRE(exp.hasValue());
    REQUIRE(scanner.singers().size() == 1);
    REQUIRE(scanner.singers()[0].ref.packageId == packageId);

    // findSinger 应能按 Unicode packageId 查找
    auto ref = scanner.findSinger("test_singer", packageId, {});
    REQUIRE(ref.hasValue());
    REQUIRE(ref->packageId == packageId);

    // packageDirectories 应能按 Unicode packageId 查找
    auto dirs = scanner.packageDirectories(packageId);
    REQUIRE(dirs.size() == 1);
}

// ===========================================================================
// 增量热更新场景：首次扫描后新增包，二次扫描应发现两个
//
// 模拟 ds-editor-lite PackageManager 的 refreshInstalledPackages() 流程：
// 用户在已打开编辑器后安装新包，触发二次刷新应能看到新增包。
// ===========================================================================

TEST_CASE("VoicebankScanner incremental: new package detected on re-scan",
          "[ds-bank][edge][incremental]") {
    TempDir dir("incr-add");
    createPackage(dir.path / "pkg_a", "pkg.a", "1.0.0");

    VoicebankScanner scanner;
    scanner.setSearchPaths({dir.path});
    auto exp1 = scanner.refresh();
    REQUIRE(exp1.hasValue());
    REQUIRE(scanner.singers().size() == 1);
    REQUIRE(scanner.singers()[0].ref.packageId == "pkg.a");

    // 模拟用户安装新包：在搜索路径下新增一个包目录
    createPackage(dir.path / "pkg_b", "pkg.b", "2.0.0");

    // 二次扫描应发现两个包
    auto exp2 = scanner.refresh();
    REQUIRE(exp2.hasValue());
    REQUIRE(scanner.singers().size() == 2);

    auto pkgIds = std::set<std::string>{
        scanner.singers()[0].ref.packageId,
        scanner.singers()[1].ref.packageId
    };
    REQUIRE(pkgIds.count("pkg.a") == 1);
    REQUIRE(pkgIds.count("pkg.b") == 1);
}

// ===========================================================================
// 增量热更新场景：替换包内容（版本升级），二次扫描应反映新版本
// ===========================================================================

TEST_CASE("VoicebankScanner incremental: replaced package reflects new version",
          "[ds-bank][edge][incremental]") {
    TempDir dir("incr-replace");
    const auto pkgDir = dir.path / "pkg";
    createPackage(pkgDir, "pkg.replace", "1.0.0");

    VoicebankScanner scanner;
    scanner.setSearchPaths({dir.path});
    scanner.refresh();
    REQUIRE(scanner.singers().size() == 1);
    REQUIRE(scanner.singers()[0].ref.version == "1.0");

    // 模拟包升级：删除旧目录内容，写入新版本
    std::error_code ec;
    std::filesystem::remove_all(pkgDir, ec);
    createPackage(pkgDir, "pkg.replace", "2.5.0");

    // 二次扫描应反映新版本
    auto exp2 = scanner.refresh();
    REQUIRE(exp2.hasValue());
    REQUIRE(scanner.singers().size() == 1);
    REQUIRE(scanner.singers()[0].ref.packageId == "pkg.replace");
    REQUIRE(scanner.singers()[0].ref.version == "2.5");
}

// ===========================================================================
// 增量热更新场景：删除包，二次扫描应不再包含该包
// ===========================================================================

TEST_CASE("VoicebankScanner incremental: deleted package disappears on re-scan",
          "[ds-bank][edge][incremental]") {
    TempDir dir("incr-delete");
    const auto pkgDirA = dir.path / "pkg_a";
    const auto pkgDirB = dir.path / "pkg_b";
    createPackage(pkgDirA, "pkg.a", "1.0.0");
    createPackage(pkgDirB, "pkg.b", "2.0.0");

    VoicebankScanner scanner;
    scanner.setSearchPaths({dir.path});
    scanner.refresh();
    REQUIRE(scanner.singers().size() == 2);

    // 模拟用户卸载包 A：删除 desc.json（比 remove_all 整个目录更可靠，
    // 因为 scanner 只检查 desc.json 是否存在来判定是否为包目录）。
    std::error_code ec;
    std::filesystem::remove(pkgDirA / "desc.json", ec);

    // 二次扫描应只剩包 B
    auto exp2 = scanner.refresh();
    REQUIRE(exp2.hasValue());
    REQUIRE(scanner.singers().size() == 1);
    REQUIRE(scanner.singers()[0].ref.packageId == "pkg.b");

    // packageDirectory("pkg.a") 应返回空
    REQUIRE(scanner.packageDirectories("pkg.a").empty());
    auto dirs = scanner.packageDirectories("pkg.a");
    REQUIRE(dirs.empty());
}

// ===========================================================================
// 超长路径：包目录路径深度较大（验证不崩溃）
// ===========================================================================

TEST_CASE("VoicebankScanner deep nested package path is scanned",
          "[ds-bank][edge][path][deep]") {
    TempDir dir("deep-path");
    // 构造一个深度为 8 的子目录结构
    std::filesystem::path deep = dir.path;
    for (int i = 0; i < 8; ++i) {
        deep = deep / ("level" + std::to_string(i));
    }
    std::filesystem::create_directories(deep);
    createPackage(deep, "pkg.deep", "1.0.0");

    // 搜索路径设为深层目录的根：scanner 会先检查 direct desc.json，
    // 然后扫描直接子目录。此处 deep 本身就是包目录。
    VoicebankScanner scanner;
    scanner.setSearchPaths({deep});
    auto exp = scanner.refresh();
    REQUIRE(exp.hasValue());
    REQUIRE(scanner.singers().size() == 1);
    REQUIRE(scanner.singers()[0].ref.packageId == "pkg.deep");
}

// ===========================================================================
// 空字符串/特殊字符 version 字段：验证不崩溃
// ===========================================================================

TEST_CASE("VoicebankScanner empty version string in manifest parses",
          "[ds-bank][edge][version][empty]") {
    TempDir dir("empty-version");
    // desc.json 中 version 字段为空字符串
    std::string desc = "{\n";
    desc += "    \"id\": \"pkg.emptyver\",\n";
    desc += "    \"version\": \"\",\n";
    desc += "    \"contributes\": {\n";
    desc += "        \"singers\": [\"characters/singer/config.json\"],\n";
    desc += "        \"inferences\": [\"inferences/duration/config.json\"]\n";
    desc += "    }\n";
    desc += "}\n";
    writeFile(dir.path / "pkg" / "desc.json", desc);

    // 写入 singer/inference 配置（复用 createPackage 的内容，但不写 desc.json）
    std::string singer = "{\n";
    singer += "    \"$version\": \"1.0\",\n";
    singer += "    \"id\": \"test_singer\",\n";
    singer += "    \"level\": 1,\n";
    singer += "    \"imports\": [{\"inferenceId\": \"duration\"}],\n";
    singer += "    \"configuration\": {\n";
    singer += "        \"defaultLanguage\": \"cmn\",\n";
    singer += "        \"languages\": [{\"id\": \"cmn\", \"g2p\": \"g2p-cmn-official\", \"s2pMode\": \"dict\"}]\n";
    singer += "    }\n";
    singer += "}\n";
    writeFile(dir.path / "pkg" / "characters" / "singer" / "config.json", singer);

    std::string inference = "{\n";
    inference += "    \"id\": \"duration\",\n";
    inference += "    \"class\": \"ai.svs.DurationInference\",\n";
    inference += "    \"level\": 1,\n";
    inference += "    \"configuration\": {}\n";
    inference += "}\n";
    writeFile(dir.path / "pkg" / "inferences" / "duration" / "config.json", inference);

    VoicebankScanner scanner;
    scanner.setSearchPaths({dir.path});
    auto exp = scanner.refresh();
    REQUIRE(exp.hasValue());
    // 无论包是否有效，都不应崩溃
    if (!scanner.singers().empty()) {
        // 若成功解析，version 字段记录其实际值
        const auto &stored = scanner.singers()[0].ref.version;
        INFO("empty version stored as: '" << stored << "'");
    }
}

// ===========================================================================
// 重复 dependencies 条目：验证原样保留（不去重）
// ===========================================================================

TEST_CASE("VoicebankScanner duplicate dependency entries are preserved",
          "[ds-bank][edge][deps][duplicate]") {
    TempDir dir("dup-deps");
    // pkg.dup 依赖 pkg.other 两次（manifest 中重复声明）
    createPackageWithDeps(dir.path / "pkg", "pkg.dup", "1.0.0",
                          {"pkg.other", "pkg.other"});
    createPackage(dir.path / "other", "pkg.other", "1.0.0");

    VoicebankScanner scanner;
    scanner.setSearchPaths({dir.path});
    auto exp = scanner.refresh();
    REQUIRE(exp.hasValue());

    const auto *status = findStatus(exp.value(), "pkg.dup");
    REQUIRE(status != nullptr);
    REQUIRE(status->valid);
    // dependencies 原样保留，不去重（VoicebankScanner 不做归一化）
    REQUIRE(status->dependencies.size() == 2);
    REQUIRE(status->dependencies[0] == "pkg.other");
    REQUIRE(status->dependencies[1] == "pkg.other");
}

// ===========================================================================
// 同一搜索路径下同 packageId 同 version 的两个目录：两者都被解析
// （VoicebankScanner 不去重，PackageCatalog 上层负责去重）
// ===========================================================================

TEST_CASE("VoicebankScanner same packageId version in two dirs both parsed",
          "[ds-bank][edge][duplicate][same-version]") {
    TempDir dir("same-id-ver");
    // 两个目录都声明同样的 packageId + version
    createPackage(dir.path / "pkg_dir1", "pkg.same", "1.0.0");
    createPackage(dir.path / "pkg_dir2", "pkg.same", "1.0.0");

    VoicebankScanner scanner;
    scanner.setSearchPaths({dir.path});
    auto exp = scanner.refresh();
    REQUIRE(exp.hasValue());

    // 两个 singer snapshot 都存在（VoicebankScanner 不做跨包去重）
    REQUIRE(scanner.singers().size() == 2);
    for (const auto &s : scanner.singers()) {
        REQUIRE(s.ref.packageId == "pkg.same");
        REQUIRE(s.ref.version == "1.0");
    }

    // packageDirectories 应只有一条记录（同 packageId+version 会合并）
    // 参见 VoicebankScanner.cpp 第 129-138 行：同 (packageId, version)
    // 的二次出现会覆盖第一次的 path
    auto dirs = scanner.packageDirectories("pkg.same");
    REQUIRE(dirs.size() == 1);
    REQUIRE(dirs[0].version.toString() == "1.0");
}
