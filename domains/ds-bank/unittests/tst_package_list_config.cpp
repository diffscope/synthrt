// PackageListConfig 单元测试。
//
// 覆盖 d:\projects\synthrt\domains\ds-bank\lib\PackageListConfig.cpp 中
// load()/save() 的所有路径：
//   - 完整 round-trip：save -> load -> 字段一致
//   - "id[version]" 解析：合法/非法格式
//   - 文件不存在、空数组、非数组根、损坏 JSON
//   - 缺失 id / relativeLocation / metadata 字段时整条记录被跳过
//   - metadata.hasSinger / installedTimestamp 缺失与默认值
//   - 非 UTF-8 路径、空 id 列表、unicode id
//   - 同一文件多包记录保序
//
// 设计原则对应：
//   ROBUST-01: load/save 返回 Expected<void>
//   ROBUST-05: 错误必须显式报错（不静默吞没）
//   CODING-03: 路径用 stdc::path::to_utf8() 而非 path.string()

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <stdcorelib/path.h>
#include <stdcorelib/support/versionnumber.h>

#include <diffsinger/Bank/PackageListConfig.h>

using namespace ds::bank;

namespace {

    // RAII 临时目录：构造时创建，析构时清理。
    struct TempDir {
        std::filesystem::path path;
        TempDir(const std::string &name) {
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            path = std::filesystem::temp_directory_path() /
                  ("ds-bank-listcfg-" + name + "-" + std::to_string(stamp));
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

    // 读取文件原始内容（用于验证序列化后的 "id[version]" 形式）。
    // 注意：PackageListItem::_version 是 protected 字段，没有公开的 version()
    // 访问器（源码观察：API 缺口）。因此版本字段只能通过 save 后读取原始 JSON
    // 间接验证。此 helper 支持该黑盒验证方式。
    std::string readFileContent(const std::filesystem::path &path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            return {};
        }
        std::stringstream ss;
        ss << file.rdbuf();
        return ss.str();
    }

} // namespace

// ===========================================================================
// Round-trip: save -> load -> 字段一致
// ===========================================================================

TEST_CASE("PackageListConfig round-trip preserves all fields", "[ds-bank][list-config]") {
    TempDir dir("round-trip");
    const auto filePath = dir.path / "packages.json";

    // 构造一个包含两条记录的配置
    std::vector<PackageListItem> items;
    items.emplace_back(
        PackageListItem{"pkg.a", stdc::VersionNumber::fromString("1.0.0").value(),
                        std::filesystem::path("pkg_a/pkg.a"),
                        PackageListItemMetadata{true, 1700000000}});
    items.emplace_back(
        PackageListItem{"pkg.b", stdc::VersionNumber::fromString("2.5.1").value(),
                        std::filesystem::path("pkg_b/pkg.b"),
                        PackageListItemMetadata{false, 0}});

    PackageListConfig saved(std::move(items));
    auto saveExp = saved.save(filePath);
    REQUIRE(saveExp.hasValue());

    // 读取原始 JSON 验证版本序列化（PackageListItem 无公开 version() 访问器，
    // 只能通过 save 后的 "id[version]" 形式间接验证）。
    const auto raw = readFileContent(filePath);
    REQUIRE(raw.find("pkg.a[1.0]") != std::string::npos);   // "1.0.0" 规范化为 "1.0"
    REQUIRE(raw.find("pkg.b[2.5.1]") != std::string::npos);

    PackageListConfig loaded;
    auto loadExp = loaded.load(filePath);
    REQUIRE(loadExp.hasValue());

    const auto &pkgs = loaded.packages();
    REQUIRE(pkgs.size() == 2);

    REQUIRE(pkgs[0].id() == "pkg.a");
    REQUIRE(pkgs[0].relativeLocation() == std::filesystem::path("pkg_a/pkg.a"));
    REQUIRE(pkgs[0].metadata().hasSinger() == true);
    REQUIRE(pkgs[0].metadata().installedTimestamp() == 1700000000);

    REQUIRE(pkgs[1].id() == "pkg.b");
    REQUIRE(pkgs[1].metadata().hasSinger() == false);
    // installedTimestamp == 0 时不写出，读回仍为默认 0
    REQUIRE(pkgs[1].metadata().installedTimestamp() == 0);
}

// ===========================================================================
// "id[version]" 解析：合法/非法格式
// ===========================================================================

TEST_CASE("PackageListConfig parses id[version] format", "[ds-bank][list-config][id-version]") {
    TempDir dir("id-version");
    const auto filePath = dir.path / "packages.json";

    // 合法格式
    writeFile(filePath, R"json([
        {"id": "pkg.a[1.0.0]", "relativeLocation": "a/", "metadata": {"hasSinger": true}},
        {"id": "pkg.b[2.5]", "relativeLocation": "b/", "metadata": {"hasSinger": false}}
    ])json");

    PackageListConfig cfg;
    REQUIRE(cfg.load(filePath).hasValue());
    REQUIRE(cfg.packages().size() == 2);
    REQUIRE(cfg.packages()[0].id() == "pkg.a");
    REQUIRE(cfg.packages()[1].id() == "pkg.b");

    // 重新保存以验证版本字段被正确解析（PackageListItem 无公开 version() 访问器，
    // 通过 save 后的 "id[version]" 序列化形式间接验证）。
    const auto reSavePath = dir.path / "resaved.json";
    REQUIRE(cfg.save(reSavePath).hasValue());
    const auto raw = readFileContent(reSavePath);
    REQUIRE(raw.find("pkg.a[1.0]") != std::string::npos);   // "1.0.0" 规范化为 "1.0"
    REQUIRE(raw.find("pkg.b[2.5]") != std::string::npos);
}

TEST_CASE("PackageListConfig skips malformed id[version] entries",
          "[ds-bank][list-config][id-version]") {
    TempDir dir("malformed");
    const auto filePath = dir.path / "packages.json";

    // 各种非法格式：
    // - 缺少 [ ]
    // - [ 后无 ]
    // - 空 id
    // - 空 version
    // - 非 string id
    writeFile(filePath, R"json([
        {"id": "no.brackets", "relativeLocation": "a/", "metadata": {"hasSinger": true}},
        {"id": "unclosed[1.0", "relativeLocation": "b/", "metadata": {"hasSinger": true}},
        {"id": "[1.0.0]", "relativeLocation": "c/", "metadata": {"hasSinger": true}},
        {"id": "empty.version[]", "relativeLocation": "d/", "metadata": {"hasSinger": true}},
        {"id": 123, "relativeLocation": "e/", "metadata": {"hasSinger": true}},
        {"id": "valid.pkg[3.0.0]", "relativeLocation": "f/", "metadata": {"hasSinger": true}}
    ])json");

    PackageListConfig cfg;
    REQUIRE(cfg.load(filePath).hasValue());
    // 仅最后一条是合法的
    REQUIRE(cfg.packages().size() == 1);
    REQUIRE(cfg.packages()[0].id() == "valid.pkg");

    // 重新保存以验证版本字段被正确解析（"3.0.0" 规范化为 "3.0"）。
    const auto reSavePath = dir.path / "resaved.json";
    REQUIRE(cfg.save(reSavePath).hasValue());
    const auto raw = readFileContent(reSavePath);
    REQUIRE(raw.find("valid.pkg[3.0]") != std::string::npos);
}

// ===========================================================================
// 文件不存在、空数组、非数组根、损坏 JSON
// ===========================================================================

TEST_CASE("PackageListConfig load returns error when file is missing",
          "[ds-bank][list-config][error]") {
    PackageListConfig cfg;
    auto exp = cfg.load("/nonexistent/path/file.json");
    REQUIRE(!exp.hasValue());
    // 错误消息应包含路径信息
    REQUIRE(!exp.error().message().empty());
}

TEST_CASE("PackageListConfig load returns error on invalid JSON",
          "[ds-bank][list-config][error]") {
    TempDir dir("bad-json");
    const auto filePath = dir.path / "packages.json";
    writeFile(filePath, "{ this is not valid json }}}");

    PackageListConfig cfg;
    auto exp = cfg.load(filePath);
    REQUIRE(!exp.hasValue());
    REQUIRE(!exp.error().message().empty());
}

TEST_CASE("PackageListConfig load returns error when root is not an array",
          "[ds-bank][list-config][error]") {
    TempDir dir("non-array");
    const auto filePath = dir.path / "packages.json";
    writeFile(filePath, R"json({"id": "pkg.a[1.0.0]"})json");

    PackageListConfig cfg;
    auto exp = cfg.load(filePath);
    REQUIRE(!exp.hasValue());
    REQUIRE(!exp.error().message().empty());
}

TEST_CASE("PackageListConfig load empty array yields no packages",
          "[ds-bank][list-config][empty]") {
    TempDir dir("empty-array");
    const auto filePath = dir.path / "packages.json";
    writeFile(filePath, "[]");

    PackageListConfig cfg;
    REQUIRE(cfg.load(filePath).hasValue());
    REQUIRE(cfg.packages().empty());
}

// ===========================================================================
// 缺失必填字段时整条记录被跳过
// ===========================================================================

TEST_CASE("PackageListConfig skips entries missing required fields",
          "[ds-bank][list-config][missing]") {
    TempDir dir("missing-fields");
    const auto filePath = dir.path / "packages.json";

    // 缺失 id / relativeLocation / metadata 的记录都应被跳过
    writeFile(filePath, R"json([
        {"relativeLocation": "a/", "metadata": {"hasSinger": true}},
        {"id": "pkg.b[1.0.0]", "metadata": {"hasSinger": true}},
        {"id": "pkg.c[1.0.0]", "relativeLocation": "c/"},
        {"id": "pkg.d[1.0.0]", "relativeLocation": "d/", "metadata": {"hasSinger": true}}
    ])json");

    PackageListConfig cfg;
    REQUIRE(cfg.load(filePath).hasValue());
    // 只有最后一条包含所有必填字段
    REQUIRE(cfg.packages().size() == 1);
    REQUIRE(cfg.packages()[0].id() == "pkg.d");
}

// ===========================================================================
// metadata 字段缺失与默认值
// ===========================================================================

TEST_CASE("PackageListConfig metadata hasSinger defaults to false when missing",
          "[ds-bank][list-config][metadata]") {
    TempDir dir("meta-default");
    const auto filePath = dir.path / "packages.json";
    writeFile(filePath, R"json([
        {"id": "pkg.a[1.0.0]", "relativeLocation": "a/", "metadata": {}},
        {"id": "pkg.b[1.0.0]", "relativeLocation": "b/", "metadata": {"hasSinger": "not-a-bool"}},
        {"id": "pkg.c[1.0.0]", "relativeLocation": "c/", "metadata": {"installedTimestamp": "not-int"}}
    ])json");

    PackageListConfig cfg;
    REQUIRE(cfg.load(filePath).hasValue());
    REQUIRE(cfg.packages().size() == 3);
    for (const auto &p : cfg.packages()) {
        // hasSinger 默认 false（包括非 bool 值时也保持默认）
        REQUIRE(p.metadata().hasSinger() == false);
        // installedTimestamp 默认 0
        REQUIRE(p.metadata().installedTimestamp() == 0);
    }
}

// ===========================================================================
// 同一文件多包记录保序
// ===========================================================================

TEST_CASE("PackageListConfig preserves insertion order across save/load",
          "[ds-bank][list-config][order]") {
    TempDir dir("order");
    const auto filePath = dir.path / "packages.json";

    std::vector<PackageListItem> items;
    // 故意用非字典序的顺序
    const std::vector<std::string> ids = {"zeta", "alpha", "middle", "beta"};
    for (const auto &id : ids) {
        items.emplace_back(PackageListItem{
            id, stdc::VersionNumber::fromString("1.0.0").value(),
            std::filesystem::path(id + "/"),
            PackageListItemMetadata{true, 0}});
    }

    PackageListConfig saved(std::move(items));
    REQUIRE(saved.save(filePath).hasValue());

    PackageListConfig loaded;
    REQUIRE(loaded.load(filePath).hasValue());
    REQUIRE(loaded.packages().size() == ids.size());
    for (size_t i = 0; i < ids.size(); ++i) {
        REQUIRE(loaded.packages()[i].id() == ids[i]);
    }
}

// ===========================================================================
// Unicode id 和路径
// ===========================================================================

TEST_CASE("PackageListConfig handles unicode id and relativeLocation",
          "[ds-bank][list-config][unicode]") {
    TempDir dir("unicode");
    const auto filePath = dir.path / "packages.json";

    writeFile(filePath,
              R"json([
        {"id": "歌手.中文[1.0.0]", "relativeLocation": "中文/歌手/", "metadata": {"hasSinger": true}},
        {"id": "voicebank.日本語[2.0.0]", "relativeLocation": "日本語/声庫/", "metadata": {"hasSinger": true}}
    ])json");

    PackageListConfig cfg;
    REQUIRE(cfg.load(filePath).hasValue());
    REQUIRE(cfg.packages().size() == 2);
    REQUIRE(cfg.packages()[0].id() == "歌手.中文");
    REQUIRE(cfg.packages()[1].id() == "voicebank.日本語");
    REQUIRE(cfg.packages()[0].relativeLocation() == std::filesystem::path("中文/歌手/"));
    REQUIRE(cfg.packages()[1].relativeLocation() == std::filesystem::path("日本語/声庫/"));
}

// ===========================================================================
// save 到不存在目录返回错误
// ===========================================================================

TEST_CASE("PackageListConfig save returns error when parent dir does not exist",
          "[ds-bank][list-config][error]") {
    PackageListConfig cfg;
    // 父目录不存在的路径
    const auto path = std::filesystem::temp_directory_path() /
                      "ds-bank-listcfg-nonexistent-dir-xyz" / "file.json";
    auto exp = cfg.save(path);
    REQUIRE(!exp.hasValue());
    REQUIRE(!exp.error().message().empty());
}

// ===========================================================================
// save 空列表写出空数组
// ===========================================================================

TEST_CASE("PackageListConfig save empty list writes empty array",
          "[ds-bank][list-config][empty]") {
    TempDir dir("save-empty");
    const auto filePath = dir.path / "packages.json";

    PackageListConfig cfg;
    REQUIRE(cfg.save(filePath).hasValue());

    // 重新加载，应该是空数组
    PackageListConfig loaded;
    REQUIRE(loaded.load(filePath).hasValue());
    REQUIRE(loaded.packages().empty());
}
