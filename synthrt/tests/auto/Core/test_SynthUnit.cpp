#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <stdcorelib/plugin/plugin.h>

#include <synthrt/Core/ContribCategory.h>
#include <synthrt/Core/ContribCreateContext.h>
#include <synthrt/Core/ContribInterpreterPlugin.h>
#include <synthrt/Core/ContribSpec.h>
#include <synthrt/Core/ContribSpecSubObjects.h>
#include <synthrt/Core/PackageHandle.h>
#include <synthrt/Core/SynthUnit.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

namespace fs = std::filesystem;

namespace {

    constexpr const char *testCategoryName = "com.example.test";
    constexpr const char *testInterpreterIid = "com.example.TestInterpreter";
    constexpr const char *testInterface = "com.example.Contract";

    int pluginCreateCount = 0;
    int exportsCount = 0;
    int configurationCount = 0;
    int optionsCount = 0;
    int validationCount = 0;
    bool rejectImports = false;

    class TestExports final : public srt::ContribExports {};
    class TestConfiguration final : public srt::ContribConfiguration {};
    class TestOptions final : public srt::ContribImportOptions {};

    class TestInterpreter final : public srt::ContribInterpreter {
    public:
        srt::Expected<std::unique_ptr<srt::ContribExports>>
            createExports(const srt::ContribSpec &) const override {
            ++exportsCount;
            return std::unique_ptr<srt::ContribExports>(new TestExports());
        }

        srt::Expected<std::unique_ptr<srt::ContribConfiguration>>
            createConfiguration(const srt::ContribSpec &) const override {
            ++configurationCount;
            return std::unique_ptr<srt::ContribConfiguration>(new TestConfiguration());
        }

        srt::Expected<std::unique_ptr<srt::ContribImportOptions>>
            createImportOptions(const srt::ContribSpec &, const srt::JsonValue &) const override {
            ++optionsCount;
            return std::unique_ptr<srt::ContribImportOptions>(new TestOptions());
        }

        srt::Expected<void> validateImports(const srt::ContribSpec &spec) const override {
            ++validationCount;
            if (rejectImports) {
                return srt::Error(srt::Error::InvalidFormat, "test interpreter rejected imports");
            }
            for (const auto &item : spec.imports()) {
                if (!item.options()) {
                    return srt::Error(srt::Error::InvalidFormat, "test import was not interpreted");
                }
            }
            return {};
        }
    };

    class TestInterpreterPlugin final : public srt::ContribInterpreterPlugin {
    public:
        srt::Expected<std::unique_ptr<srt::ContribInterpreter>> create() override {
            ++pluginCreateCount;
            return std::unique_ptr<srt::ContribInterpreter>(new TestInterpreter());
        }
    };

    class TestSpec final : public srt::ContribSpec {
    public:
        explicit TestSpec(const srt::ContribCreateContext &context) : ContribSpec(context) {
        }
    };

    class TestCategory final : public srt::ContribCategory {
    public:
        TestCategory() : ContribCategory(testCategoryName, ModuleDeclaration, testInterpreterIid) {
        }

    protected:
        srt::Expected<std::unique_ptr<srt::ContribSpec>>
            createSpec(const srt::ContribCreateContext &context) const override {
            return std::unique_ptr<srt::ContribSpec>(new TestSpec(context));
        }
    };

    class EntrySpec final : public srt::ContribSpec {
    public:
        EntrySpec(const srt::ContribCreateContext &context, std::string value)
            : ContribSpec(context), m_value(std::move(value)) {
        }

        const std::string &value() const {
            return m_value;
        }

    private:
        std::string m_value;
    };

    class EntryCategory final : public srt::ContribCategory {
    public:
        EntryCategory() : ContribCategory("com.example.entry", EntryOnly) {
        }

    protected:
        srt::Expected<std::unique_ptr<srt::ContribSpec>>
            createSpec(const srt::ContribCreateContext &context) const override {
            const auto &entry = context.manifestEntry();
            const auto value = entry.find("value");
            if (entry.size() != 2 || value == entry.end() || !value->second.isString()) {
                return srt::Error(srt::Error::InvalidFormat, "invalid test entry contribution");
            }
            return std::unique_ptr<srt::ContribSpec>(
                new EntrySpec(context, value->second.toString()));
        }
    };

    STDC_EXPORT_STATIC_PLUGIN(TestInterpreterPlugin, testInterpreterIid,
                              (stdc::json::Object{
                                  {"iid", testInterpreterIid},
                                  {"name", "test-interpreter"},
                                  {
                                   "metadata", stdc::json::Object{
                                          {"interpreters",
                                           stdc::json::Array{
                                               stdc::json::Object{{"interface", testInterface},
                                                                  {"variant", "test"},
                                                                  {"level", 1}},
                                           }}},
                                   },
    }))

    class TemporaryDirectory {
    public:
        TemporaryDirectory() {
            static int sequence = 0;
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            m_path = fs::temp_directory_path() /
                     ("synthrt-auto-" + std::to_string(stamp) + "-" + std::to_string(sequence++));
            fs::create_directories(m_path);
        }

        ~TemporaryDirectory() {
            std::error_code error;
            fs::remove_all(m_path, error);
        }

        const fs::path &path() const {
            return m_path;
        }

    private:
        fs::path m_path;
    };

    void writeText(const fs::path &path, const std::string &text) {
        fs::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary);
        BOOST_REQUIRE(stream.is_open());
        stream << text;
        BOOST_REQUIRE(stream.good());
    }

    fs::path writePackage(const fs::path &parent, std::string directory, std::string id,
                          std::string version, std::string compatVersion = {},
                          std::string dependencies = "[]", std::string imports = "[]",
                          std::string extraDesc = {}, std::string extraModule = {}) {
        const auto root = parent / directory;
        std::string compat;
        if (!compatVersion.empty()) {
            compat = R"(,"compatVersion":")" + compatVersion + '"';
        }
        writeText(root / "desc.json", R"({"$version":"1.0","id":")" + id + R"(","version":")" +
                                          version + '"' + compat + R"(,"runtimeLevel":1)" +
                                          extraDesc + R"(,"dependencies":)" + dependencies +
                                          R"(,"contributes":{")" + testCategoryName +
                                          R"(":[{"id":"main","path":"module.json"}]}})");
        writeText(root / "module.json",
                  R"({"interface":")" + std::string(testInterface) +
                      R"(","variant":"test","level":1,"exports":{},"configuration":{})" +
                      extraModule + R"(,"imports":)" + imports + '}');
        return root;
    }

    srt::SynthUnit makeUnit() {
        srt::SynthUnit unit;
        auto result = unit.addCategory(std::make_unique<TestCategory>());
        BOOST_REQUIRE(result);
        return unit;
    }

}

BOOST_AUTO_TEST_SUITE(test_SynthUnit)

BOOST_AUTO_TEST_CASE(test_data_only_expands_variables_without_loading_plugin) {
    TemporaryDirectory temporary;
    const auto root = temporary.path() / "root";
    writeText(root / "desc.json",
              R"({
                  "$version":"1.0",
                  "id":"root",
                  "version":"1",
                  "runtimeLevel":1,
                  "vars":[
                    {"name":"assets","value":"${root}/assets"},
                    {"name":"selector","value":"target"},
                    {"name":"target","value":"Expanded"},
                    {"name":"modulePath","value":"module.json"}
                  ],
                  "vendor":"${${selector}}",
                  "readme":"${assets}/readme.txt",
                  "contributes":{"com.example.test":[
                    {"id":"main","path":"${modulePath}"}
                  ]}
              })");
    writeText(root / "module.json",
              R"({
                  "interface":"com.example.Contract",
                  "variant":"test",
                  "level":1,
                  "vars":[{"name":"d","value":"$"}],
                  "name":"$${target}",
                  "exports":{},
                  "configuration":{"assets":"${assets}"}
              })");

    auto unit = makeUnit();
    const auto createCount = pluginCreateCount;
    auto opened = unit.openPackage(root, srt::SynthUnit::DataOnly);
    BOOST_REQUIRE(opened);
    auto package = opened.take();

    BOOST_CHECK(!package.isLoaded());
    BOOST_CHECK_EQUAL(package.id(), "root");
    BOOST_CHECK(package.version() == stdc::VersionNumber(1));
    BOOST_CHECK(package.compatVersion() == stdc::VersionNumber(1));
    BOOST_CHECK_EQUAL(package.runtimeLevel(), 1);
    BOOST_CHECK_EQUAL(package.vendor().text(), "Expanded");
    BOOST_CHECK_EQUAL(package.readme().text(),
                      (root / "assets" / "readme.txt").lexically_normal().string());

    auto *spec = package.contribution(testCategoryName, "main");
    BOOST_REQUIRE(spec);
    BOOST_CHECK_EQUAL(spec->name().text(), "${target}");
    BOOST_CHECK_EQUAL(spec->manifestConfiguration()["assets"].toString(),
                      root.lexically_normal().string() + "/assets");
    BOOST_CHECK(!spec->exports());
    BOOST_CHECK(!spec->configuration());
    BOOST_CHECK_EQUAL(pluginCreateCount, createCount);
    BOOST_CHECK(unit.loadedPackages().empty());
    BOOST_CHECK(unit.category(testCategoryName)->contributions().empty());
}

BOOST_AUTO_TEST_CASE(test_load_resolves_dependencies_and_commits_once) {
    TemporaryDirectory temporary;
    const auto firstPath = temporary.path() / "packages-1";
    const auto secondPath = temporary.path() / "packages-2";
    writePackage(firstPath, "dep-1", "dep", "1", "1");
    writePackage(firstPath, "dep-2", "dep", "2", "1");
    writePackage(secondPath, "dep-3", "dep", "3", "1");
    const auto root = writePackage(
        temporary.path(), "root", "root", "1", {}, R"([{"id":"dep","version":"1"}])",
        R"([{"ref":"dep:com.example.test/main"},{"ref":"dep:com.example.test/main"}])");

    auto unit = makeUnit();
    const std::vector<fs::path> paths = {firstPath, secondPath};
    unit.setPackagePaths(paths);
    const auto oldExports = exportsCount;
    const auto oldConfiguration = configurationCount;
    const auto oldOptions = optionsCount;
    const auto oldValidation = validationCount;

    auto opened = unit.openPackage(root, srt::SynthUnit::Load);
    BOOST_REQUIRE(opened);
    auto package = opened.take();
    BOOST_CHECK(package.isLoaded());
    BOOST_CHECK_EQUAL(unit.loadedPackages().size(), 2u);

    auto *rootSpec = package.contribution(testCategoryName, "main");
    BOOST_REQUIRE(rootSpec);
    BOOST_CHECK(rootSpec->exports());
    BOOST_CHECK(rootSpec->configuration());
    BOOST_REQUIRE_EQUAL(rootSpec->imports().size(), 2u);
    BOOST_CHECK(rootSpec->imports()[0].options());
    BOOST_CHECK(rootSpec->imports()[1].options());

    const auto dependencyReference = srt::ContribReference::fromString("dep:com.example.test/main");
    auto *dependencySpec = package.resolve(dependencyReference);
    BOOST_REQUIRE(dependencySpec);
    BOOST_CHECK(dependencySpec->package().version() == stdc::VersionNumber(2));
    BOOST_CHECK_EQUAL(exportsCount - oldExports, 2);
    BOOST_CHECK_EQUAL(configurationCount - oldConfiguration, 2);
    BOOST_CHECK_EQUAL(optionsCount - oldOptions, 2);
    BOOST_CHECK_EQUAL(validationCount - oldValidation, 2);

    auto openedAgain = unit.openPackage(root, srt::SynthUnit::Load);
    BOOST_REQUIRE(openedAgain);
    BOOST_CHECK(openedAgain.get() == package);
    BOOST_CHECK_EQUAL(exportsCount - oldExports, 2);

    openedAgain->reset();
    package.reset();
    BOOST_CHECK(unit.loadedPackages().empty());
    BOOST_CHECK(unit.category(testCategoryName)->contributions().empty());
}

BOOST_AUTO_TEST_CASE(test_dependency_cycle_reports_chain) {
    TemporaryDirectory temporary;
    const auto packages = temporary.path() / "packages";
    const auto a = writePackage(packages, "a", "a", "1", {}, R"([{"id":"b","version":"1"}])");
    writePackage(packages, "b", "b", "1", {}, R"([{"id":"a","version":"1"}])");

    auto unit = makeUnit();
    const std::vector<fs::path> paths = {packages};
    unit.setPackagePaths(paths);
    auto opened = unit.openPackage(a, srt::SynthUnit::Load);
    BOOST_REQUIRE(!opened);
    BOOST_CHECK(opened.error().code() == srt::Error::RecursiveDependency);
    BOOST_CHECK(opened.error().toString().find("a@1.0 -> b@1.0 -> a@1.0") != std::string::npos);
    BOOST_CHECK(unit.loadedPackages().empty());
}

BOOST_AUTO_TEST_CASE(test_invalid_manifest_profile_is_rejected) {
    TemporaryDirectory temporary;
    const auto duplicate = temporary.path() / "duplicate";
    writeText(duplicate / "desc.json",
              R"({"$version":"1.0","id":"first","id":"second","version":"1","runtimeLevel":1})");

    auto unit = makeUnit();
    auto opened = unit.openPackage(duplicate, srt::SynthUnit::DataOnly);
    BOOST_REQUIRE(!opened);
    BOOST_CHECK(opened.error().code() == srt::Error::InvalidFormat);
    BOOST_CHECK(opened.error().toString().find("duplicate object key") != std::string::npos);

    const auto malformedVariable = temporary.path() / "malformed-variable";
    writeText(
        malformedVariable / "desc.json",
        R"({"$version":"1.0","id":"bad","version":"1","runtimeLevel":1,"vendor":"${missing"})");
    opened = unit.openPackage(malformedVariable, srt::SynthUnit::DataOnly);
    BOOST_REQUIRE(!opened);
    BOOST_CHECK(opened.error().code() == srt::Error::InvalidFormat);
    BOOST_CHECK(unit.loadedPackages().empty());
}

BOOST_AUTO_TEST_CASE(test_failed_validation_rolls_back_package_state) {
    TemporaryDirectory temporary;
    const auto packages = temporary.path() / "packages";
    writePackage(packages, "dep", "dep", "1", "1");
    const auto root =
        writePackage(temporary.path(), "root", "root", "1", {}, R"([{"id":"dep","version":"1"}])",
                     R"([{"ref":"dep:com.example.test/main"}])");

    auto unit = makeUnit();
    const std::vector<fs::path> paths = {packages};
    unit.setPackagePaths(paths);
    rejectImports = true;
    auto opened = unit.openPackage(root, srt::SynthUnit::Load);
    rejectImports = false;

    BOOST_REQUIRE(!opened);
    BOOST_CHECK(opened.error().code() == srt::Error::InvalidFormat);
    BOOST_CHECK(unit.loadedPackages().empty());
    BOOST_CHECK(unit.category(testCategoryName)->contributions().empty());
}

BOOST_AUTO_TEST_CASE(test_shared_dependency_lives_until_every_dependent_is_released) {
    TemporaryDirectory temporary;
    const auto packages = temporary.path() / "packages";
    writePackage(packages, "dep", "dep", "1", "1");
    const auto first = writePackage(temporary.path(), "first", "first", "1", {},
                                    R"([{"id":"dep","version":"1"}])");
    const auto second = writePackage(temporary.path(), "second", "second", "1", {},
                                     R"([{"id":"dep","version":"1"}])");

    auto unit = makeUnit();
    const std::vector<fs::path> paths = {packages};
    unit.setPackagePaths(paths);
    auto firstResult = unit.openPackage(first, srt::SynthUnit::Load);
    BOOST_REQUIRE(firstResult);
    auto firstPackage = firstResult.take();
    auto secondResult = unit.openPackage(second, srt::SynthUnit::Load);
    BOOST_REQUIRE(secondResult);
    auto secondPackage = secondResult.take();
    BOOST_CHECK_EQUAL(unit.loadedPackages().size(), 3u);

    firstPackage.reset();
    BOOST_CHECK_EQUAL(unit.loadedPackages().size(), 2u);
    BOOST_CHECK(unit.findLoadedPackage("dep", stdc::VersionNumber(1)).has_value());

    secondPackage.reset();
    BOOST_CHECK(unit.loadedPackages().empty());
    BOOST_CHECK(unit.category(testCategoryName)->contributions().empty());
}

BOOST_AUTO_TEST_CASE(test_selected_dependency_failure_does_not_fall_back) {
    TemporaryDirectory temporary;
    const auto packages = temporary.path() / "packages";
    writePackage(packages, "dep-1", "dep", "1", "1");
    const auto broken = writePackage(packages, "dep-2", "dep", "2", "1");
    writeText(broken / "module.json",
              R"({"interface":"bad interface","variant":"test","level":1})");
    const auto root =
        writePackage(temporary.path(), "root", "root", "1", {}, R"([{"id":"dep","version":"1"}])");

    auto unit = makeUnit();
    const std::vector<fs::path> paths = {packages};
    unit.setPackagePaths(paths);
    auto opened = unit.openPackage(root, srt::SynthUnit::Load);

    BOOST_REQUIRE(!opened);
    BOOST_CHECK(opened.error().toString().find("selected Package failed full Probe") !=
                std::string::npos);
    BOOST_CHECK(unit.loadedPackages().empty());
}

BOOST_AUTO_TEST_CASE(test_entry_only_category_loads_without_an_interpreter) {
    TemporaryDirectory temporary;
    const auto root = temporary.path();
    writeText(root / "desc.json",
              R"({
                  "$version":"1.0",
                  "id":"entries",
                  "version":"1",
                  "runtimeLevel":1,
                  "contributes":{"com.example.entry":[{"id":"one","value":"data"}]}
              })");

    srt::SynthUnit unit;
    BOOST_REQUIRE(unit.addCategory(std::make_unique<EntryCategory>()));
    auto opened = unit.openPackage(root, srt::SynthUnit::Load);
    BOOST_REQUIRE(opened);
    auto package = opened.take();
    auto *contribution = package.contribution("com.example.entry", "one");
    BOOST_REQUIRE(contribution);
    auto *entry = contribution->as<EntrySpec>();
    BOOST_REQUIRE(entry);
    BOOST_CHECK_EQUAL(entry->value(), "data");
    BOOST_CHECK_EQUAL(unit.category("com.example.entry")->contributions().size(), 1u);

    package.reset();
    BOOST_CHECK(unit.loadedPackages().empty());
}

BOOST_AUTO_TEST_SUITE_END()
