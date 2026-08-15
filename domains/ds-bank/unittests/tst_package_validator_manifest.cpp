// PackageValidator::validate(in-memory PackageManifest) 单元测试。
//
// 现有 test_package_parser.cpp 主要覆盖 validatePackage(dir)（重新读盘 + 解析 + 校验），
// 但 PackageValidator 还提供直接对内存中 PackageManifest 校验的 validate() 接口。
// 此文件补全 validate(info, version) 的覆盖：
//   - 空 manifest（无 packageId/name/version）应报缺失错误
//   - 部分 manifest（仅 packageId）应报 name/version 缺失
//   - 完整 manifest 不应有错误
//   - singer.singerId 空时报 singers[N]: missing singerId
//   - 多 singer 中部分空 singerId
//   - SchemaVersion 各枚举值不抛异常（接口接受所有枚举）
//
// 设计原则对应：
//   ROBUST-01: validate 返回 ValidationReport（不抛异常）
//   ROBUST-05: 缺失字段必须显式报错，不静默通过

#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <diffsinger/Bank/PackageManifest.h>
#include <diffsinger/Bank/PackageValidator.h>
#include <diffsinger/Bank/SingerManifest.h>

using namespace ds::bank;

namespace {

    // 构造一个最小但完整的 PackageManifest（含 1 个 singer）
    PackageManifest makeCompleteManifest() {
        PackageManifest info;
        info.setPackageId("pkg.complete");
        info.setName("Complete Package");
        info.setVersion(stdc::VersionNumber::fromString("1.0.0").value());

        SingerManifest singer;
        singer.setSingerId("singer_a");
        singer.setName("Singer A");
        singer.setPackageId("pkg.complete");
        info.setSingers({std::move(singer)});
        return info;
    }

} // namespace

// ===========================================================================
// 空 manifest：所有必填字段缺失
// ===========================================================================

TEST_CASE("PackageValidator validate empty manifest reports missing fields",
          "[ds-bank][validator][manifest]") {
    PackageManifest info;  // 默认构造：所有字段空
    PackageValidator validator;
    auto report = validator.validate(info, PackageValidator::SchemaVersion::V10);

    REQUIRE(report.hasErrors());
    // 应该至少报 packageId / name / version 三个缺失
    bool hasPackageIdMissing = false;
    bool hasNameMissing = false;
    bool hasVersionMissing = false;
    for (const auto &item : report.items()) {
        if (item.message.find("packageId") != std::string::npos) hasPackageIdMissing = true;
        if (item.message.find("name") != std::string::npos) hasNameMissing = true;
        if (item.message.find("version") != std::string::npos) hasVersionMissing = true;
    }
    CHECK(hasPackageIdMissing);
    CHECK(hasNameMissing);
    CHECK(hasVersionMissing);
}

// ===========================================================================
// 部分 manifest：仅 packageId
// ===========================================================================

TEST_CASE("PackageValidator validate partial manifest reports name and version missing",
          "[ds-bank][validator][manifest]") {
    PackageManifest info;
    info.setPackageId("pkg.partial");
    // 不设置 name 和 version

    PackageValidator validator;
    auto report = validator.validate(info, PackageValidator::SchemaVersion::V10);

    REQUIRE(report.hasErrors());
    bool hasNameMissing = false;
    bool hasVersionMissing = false;
    for (const auto &item : report.items()) {
        if (item.message.find("name") != std::string::npos) hasNameMissing = true;
        if (item.message.find("version") != std::string::npos) hasVersionMissing = true;
    }
    CHECK(hasNameMissing);
    CHECK(hasVersionMissing);
}

// ===========================================================================
// 完整 manifest：无错误
// ===========================================================================

TEST_CASE("PackageValidator validate complete manifest has no errors",
          "[ds-bank][validator][manifest]") {
    auto info = makeCompleteManifest();
    PackageValidator validator;
    auto report = validator.validate(info, PackageValidator::SchemaVersion::V10);
    CHECK(!report.hasErrors());
}

// ===========================================================================
// singer.singerId 空时报 singers[N]: missing singerId
// ===========================================================================

TEST_CASE("PackageValidator validate reports singer with empty singerId",
          "[ds-bank][validator][manifest][singer]") {
    auto info = makeCompleteManifest();
    // 添加一个空 singerId 的 singer
    std::vector<SingerManifest> singers = info.singers();
    SingerManifest emptySinger;
    emptySinger.setPackageId("pkg.complete");
    // singerId 故意留空
    singers.push_back(std::move(emptySinger));
    info.setSingers(std::move(singers));

    PackageValidator validator;
    auto report = validator.validate(info, PackageValidator::SchemaVersion::V10);

    REQUIRE(report.hasErrors());
    bool foundSingerIdError = false;
    for (const auto &item : report.items()) {
        if (item.path.find("singers/1") != std::string::npos &&
            item.message.find("singerId") != std::string::npos) {
            foundSingerIdError = true;
        }
    }
    CHECK(foundSingerIdError);
}

TEST_CASE("PackageValidator validate reports all singers when none have singerId",
          "[ds-bank][validator][manifest][singer]") {
    PackageManifest info;
    info.setPackageId("pkg.empty-singers");
    info.setName("Empty Singers");
    info.setVersion(stdc::VersionNumber::fromString("1.0.0").value());

    // 添加 3 个空 singerId 的 singer
    std::vector<SingerManifest> singers;
    for (int i = 0; i < 3; ++i) {
        SingerManifest s;
        s.setPackageId("pkg.empty-singers");
        singers.push_back(std::move(s));
    }
    info.setSingers(std::move(singers));

    PackageValidator validator;
    auto report = validator.validate(info, PackageValidator::SchemaVersion::V10);

    REQUIRE(report.hasErrors());
    // 应该为每个 singer 报一个错误
    int singerIdErrorCount = 0;
    for (const auto &item : report.items()) {
        if (item.message.find("missing singerId") != std::string::npos) {
            ++singerIdErrorCount;
        }
    }
    CHECK(singerIdErrorCount == 3);
}

// ===========================================================================
// SchemaVersion 各枚举值不抛异常
// ===========================================================================

TEST_CASE("PackageValidator validate accepts all SchemaVersion enum values",
          "[ds-bank][validator][manifest][schema]") {
    auto info = makeCompleteManifest();
    PackageValidator validator;

    // 遍历所有 SchemaVersion 枚举值，确保不抛异常且返回 report
    const std::vector<PackageValidator::SchemaVersion> versions = {
        PackageValidator::SchemaVersion::V1,
        PackageValidator::SchemaVersion::V2,
        PackageValidator::SchemaVersion::V3,
        PackageValidator::SchemaVersion::V4,
        PackageValidator::SchemaVersion::V5,
        PackageValidator::SchemaVersion::V6,
        PackageValidator::SchemaVersion::V7,
        PackageValidator::SchemaVersion::V8,
        PackageValidator::SchemaVersion::V9,
        PackageValidator::SchemaVersion::V10,
    };
    for (const auto v : versions) {
        auto report = validator.validate(info, v);
        // 完整 manifest 不应有错误
        CHECK(!report.hasErrors());
    }
}

// ===========================================================================
// version.isEmpty() 触发缺失错误
// ===========================================================================

TEST_CASE("PackageValidator validate reports empty version", "[ds-bank][validator][manifest]") {
    auto info = makeCompleteManifest();
    info.setVersion(stdc::VersionNumber{});  // 显式设为空

    PackageValidator validator;
    auto report = validator.validate(info, PackageValidator::SchemaVersion::V10);

    REQUIRE(report.hasErrors());
    bool foundVersionError = false;
    for (const auto &item : report.items()) {
        if (item.message.find("version") != std::string::npos) {
            foundVersionError = true;
            break;
        }
    }
    CHECK(foundVersionError);
}
