// ds-editor-lite 真实使用场景模拟测试。
//
// ds-editor-lite 的 PackageCatalog / SynthrtEngine 在 VoicebankScanner 之上
// 实现了以下关键逻辑（参考 d:\projects\ds-editor-lite\src\app\Modules\
// synthrtengine\PackageCatalog.cpp）：
//   - validate(): 检测重复 package root、重复 (packageId, version)、
//     重复 singer identifier、status 与 manifest 不一致
//   - buildFingerprints(): 计算 catalogFingerprint / languageFingerprint
//     用于检测 refresh 是否真的改变了状态（unchanged 判定）
//   - prepareRefresh(allowReuse): 搜索路径未变时返回 unchanged Candidate
//   - findSinger(identifier): 跨所有包查找；重复时返回 nullptr（冲突）
//   - singerSnapshot(identifier): 按 (packageId, version, singerId) 精确查找
//
// 本文件不直接依赖 ds-editor-lite 头文件（不同项目），而是用 VoicebankScanner
// 模拟这些场景，验证底层 VoicebankScanner 的行为足以支撑 PackageCatalog 的
// 上层逻辑。这对应 INFRA-03 L1 单组件测试要求（不加载插件 DLL）。
//
// 设计原则对应：
//   ROBUST-01: VoicebankScanner::refresh() 返回 Expected
//   ROBUST-05: 重复情况由上层显式检测，VoicebankScanner 不静默吞没
//   ARCH-06: 跨包解析——singer 的 stage 可来自不同 package
//   CODING-03: 路径比较使用 lexically_normal() 避免分隔符差异

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <stdcorelib/support/versionnumber.h>

#include <diffsinger/Bank/VoicebankScanner.h>

using namespace ds::bank;

namespace {

    // RAII 临时目录：构造时创建，析构时清理。
    struct TempDir {
        std::filesystem::path path;
        TempDir(const std::string &name) {
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            path = std::filesystem::temp_directory_path() /
                  ("ds-bank-lite-" + name + "-" + std::to_string(stamp));
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
    void createPackage(const std::filesystem::path &pkgDir,
                       const std::string &packageId,
                       const std::string &version,
                       const std::string &singerId = "test_singer") {
        std::string desc = "{\n";
        desc += "    \"id\": \"" + packageId + "\",\n";
        desc += "    \"version\": \"" + version + "\",\n";
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

    // 模拟 PackageCatalog::buildFingerprints() 的指纹构造逻辑。
    // 将 (packageId, version, rootPath) 序列化为稳定字符串，用于检测两次
    // refresh 之间状态是否真的改变。排序保证顺序无关。
    std::string buildCatalogFingerprint(const std::vector<PackageStatus> &statuses) {
        // 按 (packageId, version, rootPath) 排序，与 PackageCatalog::prepareRefresh
        // 中的 std::sort 一致。
        auto sorted = statuses;
        std::sort(sorted.begin(), sorted.end(),
                  [](const PackageStatus &a, const PackageStatus &b) {
                      return std::tie(a.packageId, a.version, a.rootPath) <
                             std::tie(b.packageId, b.version, b.rootPath);
                  });

        std::ostringstream ss;
        for (const auto &s : sorted) {
            ss << s.packageId << '|';
            ss << s.version.toString() << '|';
            ss << s.rootPath.lexically_normal().generic_string() << '|';
            ss << (s.valid ? 'V' : 'I') << '|';
            ss << s.dependencies.size() << '|';
            for (const auto &d : s.dependencies) {
                ss << d << ',';
            }
            ss << '#';
        }
        return ss.str();
    }

    // 模拟 PackageCatalog::validate() 中的 duplicate (packageId, version) 检测。
    // 返回首次冲突的 (packageId, version) 字符串对；无冲突返回空。
    std::string findDuplicatePackageVersion(const std::vector<PackageStatus> &statuses) {
        std::set<std::pair<std::string, std::string>> seen;
        for (const auto &s : statuses) {
            if (!s.valid) continue;
            const auto key = std::make_pair(s.packageId, s.version.toString());
            if (!seen.insert(key).second) {
                return key.first + "[" + key.second + "]";
            }
        }
        return {};
    }

    // 模拟 PackageCatalog::validate() 中的 duplicate package root 检测。
    std::string findDuplicateRoot(const std::vector<PackageStatus> &statuses) {
        std::set<std::string> seen;
        for (const auto &s : statuses) {
            const auto root = s.rootPath.lexically_normal().generic_string();
            if (!seen.insert(root).second) {
                return root;
            }
        }
        return {};
    }

    // 模拟 PackageCatalog::validate() 中的 duplicate singer identifier 检测。
    // singer identifier = (singerId, packageId, version) 三元组。
    std::string findDuplicateSinger(const std::vector<SingerSnapshot> &singers) {
        std::set<std::tuple<std::string, std::string, std::string>> seen;
        for (const auto &s : singers) {
            const auto key = std::make_tuple(s.ref.singerId, s.ref.packageId, s.ref.version);
            if (!seen.insert(key).second) {
                return s.ref.singerId + "@" + s.ref.packageId + "[" + s.ref.version + "]";
            }
        }
        return {};
    }

} // namespace

// ===========================================================================
// 场景 1: fingerprint 不可变性——相同状态二次扫描应产生相同指纹
//
// 对应 PackageCatalog::prepareRefresh() 中 unchanged 判定：
//   candidate->catalogFingerprint == m_snapshot->catalogFingerprint
//   && m_searchPaths == validSearchPaths
// ===========================================================================

TEST_CASE("ds-editor-lite scenario: same packages produce same fingerprint on re-scan",
          "[ds-bank][lite][fingerprint]") {
    TempDir dir("fingerprint-same");
    createPackage(dir.path / "pkg_a", "pkg.a", "1.0.0");
    createPackage(dir.path / "pkg_b", "pkg.b", "2.5.0");

    VoicebankScanner scanner;
    scanner.setSearchPaths({dir.path});

    auto exp1 = scanner.refresh();
    REQUIRE(exp1.hasValue());
    const auto fp1 = buildCatalogFingerprint(exp1.value());

    // 二次扫描：状态未变，指纹应相同
    auto exp2 = scanner.refresh();
    REQUIRE(exp2.hasValue());
    const auto fp2 = buildCatalogFingerprint(exp2.value());

    REQUIRE(fp1 == fp2);
}

TEST_CASE("ds-editor-lite scenario: added package changes fingerprint",
          "[ds-bank][lite][fingerprint]") {
    TempDir dir("fingerprint-add");
    createPackage(dir.path / "pkg_a", "pkg.a", "1.0.0");

    VoicebankScanner scanner;
    scanner.setSearchPaths({dir.path});
    auto exp1 = scanner.refresh();
    REQUIRE(exp1.hasValue());
    const auto fp1 = buildCatalogFingerprint(exp1.value());

    // 新增包，指纹应变
    createPackage(dir.path / "pkg_b", "pkg.b", "2.0.0");
    auto exp2 = scanner.refresh();
    REQUIRE(exp2.hasValue());
    const auto fp2 = buildCatalogFingerprint(exp2.value());

    REQUIRE(fp1 != fp2);
}

TEST_CASE("ds-editor-lite scenario: version bump changes fingerprint",
          "[ds-bank][lite][fingerprint]") {
    TempDir dir("fingerprint-bump");
    const auto pkgDir = dir.path / "pkg";
    createPackage(pkgDir, "pkg.bump", "1.0.0");

    VoicebankScanner scanner;
    scanner.setSearchPaths({dir.path});
    auto exp1 = scanner.refresh();
    REQUIRE(exp1.hasValue());
    const auto fp1 = buildCatalogFingerprint(exp1.value());

    // 替换为新版本
    std::error_code ec;
    std::filesystem::remove_all(pkgDir, ec);
    createPackage(pkgDir, "pkg.bump", "2.0.0");
    auto exp2 = scanner.refresh();
    REQUIRE(exp2.hasValue());
    const auto fp2 = buildCatalogFingerprint(exp2.value());

    REQUIRE(fp1 != fp2);
}

// ===========================================================================
// 场景 2: 重复 package root 检测
//
// 对应 PackageCatalog::validate() 中：
//   if (!roots.insert(root).second) {
//       return Error(PackageDuplicate, "Duplicate package root: " + root);
//   }
//
// VoicebankScanner 不去重 root，但同一物理路径不会出现两次（不会有两个
// 不同的 directory_iterator 条目指向同一目录）。此场景模拟两个搜索路径
// 都包含同一物理包目录——通过符号链接或重复挂载。简化为：两个不同搜索
// 路径下放同一个包的副本，VoicebankScanner 解析两次，PackageCatalog 上层
// 检测到不同 root 路径不会触发 duplicate root（root 字符串不同）。
//
// 真正的 duplicate root 场景：同一搜索路径被传入两次。此处用两个独立
// 目录路径模拟，验证 VoicebankScanner 解析后 PackageCatalog 的 validate
// 是否会因 root 不同而不报错。
// ===========================================================================

TEST_CASE("ds-editor-lite scenario: same packageId version across two roots",
          "[ds-bank][lite][duplicate][cross-path]") {
    TempDir dir1("dup-root-1");
    TempDir dir2("dup-root-2");
    // 两个不同 root 下放置同样的 packageId + version
    createPackage(dir1.path / "pkg", "pkg.dup", "1.0.0");
    createPackage(dir2.path / "pkg", "pkg.dup", "1.0.0");

    VoicebankScanner scanner;
    scanner.setSearchPaths({dir1.path, dir2.path});
    auto exp = scanner.refresh();
    REQUIRE(exp.hasValue());

    // VoicebankScanner 不会拒绝——两个包都被解析
    REQUIRE(scanner.singers().size() == 2);

    // 模拟 PackageCatalog::validate() 检测重复 (packageId, version)
    const auto dup = findDuplicatePackageVersion(exp.value());
    REQUIRE(!dup.empty());
    REQUIRE(dup == "pkg.dup[1.0]");

    // 两个 root 路径不同，所以不是 duplicate root
    const auto dupRoot = findDuplicateRoot(exp.value());
    REQUIRE(dupRoot.empty());
}

// ===========================================================================
// 场景 3: 重复 singer identifier 检测
//
// 对应 PackageCatalog::validate() 中：
//   const auto identifier = std::make_tuple(
//       singer.ref.singerId, singer.ref.packageId, singer.ref.version);
//   if (!singerIdentifiers.insert(identifier).second) {
//       return Error(PackageDuplicate, "Duplicate singer identifier: " + ...);
//   }
// ===========================================================================

TEST_CASE("ds-editor-lite scenario: duplicate singer identifier across paths",
          "[ds-bank][lite][duplicate][singer]") {
    TempDir dir1("dup-singer-1");
    TempDir dir2("dup-singer-2");
    // 两个目录都声明 pkg.dup + 1.0.0 + test_singer
    createPackage(dir1.path / "pkg", "pkg.dup", "1.0.0", "shared_singer");
    createPackage(dir2.path / "pkg", "pkg.dup", "1.0.0", "shared_singer");

    VoicebankScanner scanner;
    scanner.setSearchPaths({dir1.path, dir2.path});
    auto exp = scanner.refresh();
    REQUIRE(exp.hasValue());
    REQUIRE(scanner.singers().size() == 2);

    // 两个 snapshot 拥有相同的 (singerId, packageId, version) 三元组
    REQUIRE(scanner.singers()[0].ref.singerId == "shared_singer");
    REQUIRE(scanner.singers()[1].ref.singerId == "shared_singer");
    REQUIRE(scanner.singers()[0].ref.packageId == "pkg.dup");
    REQUIRE(scanner.singers()[1].ref.packageId == "pkg.dup");

    // 模拟 PackageCatalog::validate() 检测重复 singer identifier
    const auto dup = findDuplicateSinger(scanner.singers());
    REQUIRE(!dup.empty());
    REQUIRE(dup.find("shared_singer") != std::string::npos);
}

// ===========================================================================
// 场景 4: findSinger 跨包查找——同 singerId 在多包中存在时返回首个匹配
//
// 对应 SynthrtEngine::findSinger() 跨包查找逻辑。
// VoicebankScanner::findSinger(singerId) 不带 packageId/version 过滤时
// 返回第一个匹配。这是 PackageCatalog::findSinger 需要检测冲突的依据。
// ===========================================================================

TEST_CASE("ds-editor-lite scenario: findSinger returns first match across packages",
          "[ds-bank][lite][find-singer]") {
    TempDir dir("find-first");
    createPackage(dir.path / "pkg_a", "pkg.a", "1.0.0", "shared_singer");
    createPackage(dir.path / "pkg_b", "pkg.b", "2.0.0", "shared_singer");

    VoicebankScanner scanner;
    scanner.setSearchPaths({dir.path});
    scanner.refresh();
    REQUIRE(scanner.singers().size() == 2);

    // 无过滤查询：返回第一个匹配
    auto ref = scanner.findSinger("shared_singer");
    REQUIRE(ref.hasValue());
    REQUIRE(ref->singerId == "shared_singer");

    // 带 packageId 过滤：分别精确定位
    auto refA = scanner.findSinger("shared_singer", "pkg.a", {});
    REQUIRE(refA.hasValue());
    REQUIRE(refA->packageId == "pkg.a");

    auto refB = scanner.findSinger("shared_singer", "pkg.b", {});
    REQUIRE(refB.hasValue());
    REQUIRE(refB->packageId == "pkg.b");

    // 模拟 PackageCatalog::Snapshot::findSinger：若 >1 个匹配则视为冲突
    // 此处通过遍历计数，验证存在 2 个匹配（冲突场景）
    int matchCount = 0;
    for (const auto &s : scanner.singers()) {
        if (s.ref.singerId == "shared_singer" && s.ref.packageId == "pkg.a") {
            ++matchCount;
        }
    }
    // pkg.a 下只有一个 shared_singer（不冲突）
    REQUIRE(matchCount == 1);
}

// ===========================================================================
// 场景 5: singerSnapshot(identifier) 按版本精确查找
//
// 对应 SynthrtEngine::singerSnapshot(identifier) 按 (packageId, version)
// 精确定位 singer 的逻辑。VoicebankScanner::singerSnapshot(ref) 中
// version 字段非空时必须语义匹配（versionsMatch）。
// ===========================================================================

TEST_CASE("ds-editor-lite scenario: singerSnapshot by exact version",
          "[ds-bank][lite][singer-snapshot][version]") {
    TempDir dir("snap-version");
    // 同 packageId 两个版本共存
    createPackage(dir.path / "pkg_v1", "pkg.multi", "1.0.0", "singer_x");
    createPackage(dir.path / "pkg_v2", "pkg.multi", "2.0.0", "singer_x");

    VoicebankScanner scanner;
    scanner.setSearchPaths({dir.path});
    scanner.refresh();
    REQUIRE(scanner.singers().size() == 2);

    // 精确版本查找 v1.0.0
    SingerRef refV1{"pkg.multi", "singer_x", "1.0.0"};
    auto snapV1 = scanner.singerSnapshot(refV1);
    REQUIRE(snapV1.hasValue());
    REQUIRE(snapV1->ref.version == "1.0");

    // 精确版本查找 v2.0.0
    SingerRef refV2{"pkg.multi", "singer_x", "2.0.0"};
    auto snapV2 = scanner.singerSnapshot(refV2);
    REQUIRE(snapV2.hasValue());
    REQUIRE(snapV2->ref.version == "2.0");

    // 错误版本：返回错误（FileNotFound）
    SingerRef refWrong{"pkg.multi", "singer_x", "9.9.9"};
    auto snapWrong = scanner.singerSnapshot(refWrong);
    REQUIRE(!snapWrong.hasValue());

    // 空 version：匹配第一个（向后兼容）
    SingerRef refEmpty{"pkg.multi", "singer_x", ""};
    auto snapEmpty = scanner.singerSnapshot(refEmpty);
    REQUIRE(snapEmpty.hasValue());

    // 错误 packageId：返回错误
    SingerRef refWrongPkg{"nonexistent", "singer_x", ""};
    REQUIRE(!scanner.singerSnapshot(refWrongPkg).hasValue());

    // 错误 singerId：返回错误
    SingerRef refWrongSinger{"pkg.multi", "nonexistent_singer", ""};
    REQUIRE(!scanner.singerSnapshot(refWrongSinger).hasValue());
}

// ===========================================================================
// 场景 6: 同 packageId 多版本共存（V3-01 §1.6）
//
// 对应 SynthrtEngine::packageDirectory(identifier) 按版本化 identifier
// 查找包目录的逻辑。VoicebankScanner::packageDirectories 保留所有版本。
// ===========================================================================

TEST_CASE("ds-editor-lite scenario: multi-version same packageId coexist",
          "[ds-bank][lite][multi-version]") {
    TempDir dir("multi-ver");
    const std::string packageId = "pkg.multiver";
    createPackage(dir.path / "pkg_v1", packageId, "1.0.0");
    createPackage(dir.path / "pkg_v2", packageId, "2.0.0");
    createPackage(dir.path / "pkg_v3", packageId, "3.0.0");

    VoicebankScanner scanner;
    scanner.setSearchPaths({dir.path});
    scanner.refresh();
    REQUIRE(scanner.singers().size() == 3);

    // packageDirectories 应返回 3 个版本
    auto dirs = scanner.packageDirectories(packageId);
    REQUIRE(dirs.size() == 3);

    // 验证三个版本都在
    std::set<std::string> versions;
    for (const auto &d : dirs) {
        versions.insert(d.version.toString());
    }
    REQUIRE(versions.count("1.0") == 1);
    REQUIRE(versions.count("2.0") == 1);
    REQUIRE(versions.count("3.0") == 1);

    // 三个路径都非空
    for (const auto &d : dirs) {
        REQUIRE(!d.path.empty());
    }

    // 模拟 SynthrtEngine 按 identifier.packageVersion 查找目录
    // identifier.packageVersion = "2.0" → 应找到 v2 目录
    const std::string targetVersion = "2.0";
    std::filesystem::path foundPath;
    for (const auto &d : dirs) {
        if (d.version.toString() == targetVersion) {
            foundPath = d.path;
            break;
        }
    }
    REQUIRE(!foundPath.empty());
    REQUIRE(foundPath.lexically_normal() ==
            (dir.path / "pkg_v2").lexically_normal());
}

// ===========================================================================
// 场景 7: 错误包不阻塞其他包解析
//
// 对应 PackageManager::refreshInstalledPackages() 遍历 catalog->packages
// 处理 status.valid / parseError 的逻辑：错误包被记录但不影响其他包。
// ===========================================================================

TEST_CASE("ds-editor-lite scenario: corrupted package does not block others",
          "[ds-bank][lite][error-isolation]") {
    TempDir dir("error-iso");
    // 包 A: 正常
    createPackage(dir.path / "pkg_a", "pkg.a", "1.0.0");
    // 包 B: desc.json 损坏
    std::filesystem::create_directories(dir.path / "pkg_b");
    writeFile(dir.path / "pkg_b" / "desc.json", "{ broken json }}}");
    // 包 C: 正常
    createPackage(dir.path / "pkg_c", "pkg.c", "3.0.0");

    VoicebankScanner scanner;
    scanner.setSearchPaths({dir.path});
    auto exp = scanner.refresh();
    REQUIRE(exp.hasValue());

    // 状态列表应包含 3 条（其中 1 条 valid=false）
    REQUIRE(exp.value().size() == 3);

    int validCount = 0;
    int invalidCount = 0;
    for (const auto &s : exp.value()) {
        if (s.valid) {
            ++validCount;
        } else {
            ++invalidCount;
        }
    }
    REQUIRE(validCount == 2);
    REQUIRE(invalidCount == 1);

    // 两个正常包的 singer 都应被解析
    REQUIRE(scanner.singers().size() == 2);
    auto pkgIds = std::set<std::string>{
        scanner.singers()[0].ref.packageId,
        scanner.singers()[1].ref.packageId
    };
    REQUIRE(pkgIds.count("pkg.a") == 1);
    REQUIRE(pkgIds.count("pkg.c") == 1);

    // 模拟 PackageManager 提取错误信息
    for (const auto &s : exp.value()) {
        if (!s.valid) {
            // 错误信息应非空（PackageStatus.error.message）
            REQUIRE(!s.error.message.empty());
        }
    }
}

// ===========================================================================
// 场景 8: 空搜索路径列表——对应首次启动未配置声库路径
// ===========================================================================

TEST_CASE("ds-editor-lite scenario: empty search paths yields empty catalog",
          "[ds-bank][lite][empty]") {
    VoicebankScanner scanner;
    scanner.setSearchPaths({});
    auto exp = scanner.refresh();
    REQUIRE(exp.hasValue());
    REQUIRE(exp.value().empty());
    REQUIRE(scanner.singers().empty());
}

// ===========================================================================
// 场景 9: 搜索路径列表中混入不存在的路径——对应配置中遗留旧路径
// ===========================================================================

TEST_CASE("ds-editor-lite scenario: nonexistent paths in list are skipped",
          "[ds-bank][lite][missing-path]") {
    TempDir dir("mixed-paths");
    createPackage(dir.path / "pkg", "pkg.exists", "1.0.0");

    VoicebankScanner scanner;
    // 混入不存在的路径和文件路径
    scanner.setSearchPaths({
        "/nonexistent/path/one",
        dir.path,
        "/nonexistent/path/two",
    });
    auto exp = scanner.refresh();
    REQUIRE(exp.hasValue());
    REQUIRE(scanner.singers().size() == 1);
    REQUIRE(scanner.singers()[0].ref.packageId == "pkg.exists");
}

// ===========================================================================
// 场景 10: 模拟 PackageCatalog allowReuse——搜索路径不变时返回 unchanged
//
// 对应 PackageCatalog::prepareRefresh(allowReuse=true) 中：
//   if (allowReuse && m_snapshot->generation != 0 &&
//       m_searchPaths == validSearchPaths) {
//       return Candidate{..., true};
//   }
//
// VoicebankScanner 不直接支持 unchanged 语义，但可以通过对比
// setSearchPaths 的输入与上次扫描结果是否一致来模拟。
// ===========================================================================

TEST_CASE("ds-editor-lite scenario: same search paths triggers allowReuse",
          "[ds-bank][lite][allow-reuse]") {
    TempDir dir("reuse");
    createPackage(dir.path / "pkg", "pkg.reuse", "1.0.0");

    VoicebankScanner scanner;
    std::vector<std::filesystem::path> paths = {dir.path};

    // 第一次 refresh：建立基线
    scanner.setSearchPaths(paths);
    auto exp1 = scanner.refresh();
    REQUIRE(exp1.hasValue());
    const auto fp1 = buildCatalogFingerprint(exp1.value());

    // 模拟 PackageCatalog::prepareRefresh(allowReuse=true)
    // 当 searchPaths 与上次相同时，可跳过 refresh 直接复用。
    // 此处验证：相同输入下再次 refresh 产生相同指纹（即 unchanged 判定成立）。
    scanner.setSearchPaths(paths);
    auto exp2 = scanner.refresh();
    REQUIRE(exp2.hasValue());
    const auto fp2 = buildCatalogFingerprint(exp2.value());

    REQUIRE(fp1 == fp2);

    // 模拟 allowReuse=false 的强制刷新场景：仍得到一致结果
    scanner.setSearchPaths(paths);
    auto exp3 = scanner.refresh();
    REQUIRE(exp3.hasValue());
    REQUIRE(buildCatalogFingerprint(exp3.value()) == fp1);
}

// ===========================================================================
// 场景 11: 模拟 findSinger 冲突——同一 (singerId, packageId, version)
// 出现两次时 PackageCatalog::findSinger 返回 nullptr
//
// VoicebankScanner::findSinger(singerId) 返回首个匹配，但 PackageCatalog
// 的 findSinger(identifier) 会在 >1 个匹配时返回 nullptr。此处验证
// VoicebankScanner 的 singers 列表确实存在重复，以便上层检测。
// ===========================================================================

TEST_CASE("ds-editor-lite scenario: conflicting singers visible for catalog to detect",
          "[ds-bank][lite][conflict]") {
    TempDir dir1("conflict-1");
    TempDir dir2("conflict-2");
    // 两处都声明 (pkg.conflict, 1.0.0, same_singer)
    createPackage(dir1.path / "pkg", "pkg.conflict", "1.0.0", "same_singer");
    createPackage(dir2.path / "pkg", "pkg.conflict", "1.0.0", "same_singer");

    VoicebankScanner scanner;
    scanner.setSearchPaths({dir1.path, dir2.path});
    auto exp = scanner.refresh();
    REQUIRE(exp.hasValue());
    REQUIRE(scanner.singers().size() == 2);

    // 两个 snapshot 拥有完全相同的 (singerId, packageId, version)
    REQUIRE(scanner.singers()[0].ref.singerId ==
            scanner.singers()[1].ref.singerId);
    REQUIRE(scanner.singers()[0].ref.packageId ==
            scanner.singers()[1].ref.packageId);
    REQUIRE(scanner.singers()[0].ref.version ==
            scanner.singers()[1].ref.version);

    // 模拟 PackageCatalog::Snapshot::findSinger(identifier) 冲突检测：
    // 遍历所有 packages 统计匹配数，>1 时返回 nullptr
    int matchCount = 0;
    const std::string targetSingerId = "same_singer";
    const std::string targetPackageId = "pkg.conflict";
    const std::string targetVersion = "1.0";
    for (const auto &s : scanner.singers()) {
        if (s.ref.singerId == targetSingerId &&
            s.ref.packageId == targetPackageId &&
            s.ref.version == targetVersion) {
            ++matchCount;
        }
    }
    REQUIRE(matchCount == 2);  // 冲突：>1 个匹配

    // 模拟 PackageCatalog::validate() 应能检测到冲突并返回错误
    const auto dupSinger = findDuplicateSinger(scanner.singers());
    REQUIRE(!dupSinger.empty());

    const auto dupPkgVer = findDuplicatePackageVersion(exp.value());
    REQUIRE(!dupPkgVer.empty());
    REQUIRE(dupPkgVer == "pkg.conflict[1.0]");
}
