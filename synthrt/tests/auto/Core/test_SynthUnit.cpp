#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <stdcorelib/plugin/plugin.h>

#include <synthrt/Core/ContribCategory.h>
#include <synthrt/Core/ContribCreateContext.h>
#include <synthrt/Core/ContribExecInstance.h>
#include <synthrt/Core/ContribInterpreterPlugin.h>
#include <synthrt/Core/ContribSpec.h>
#include <synthrt/Core/PackageHandle.h>
#include <synthrt/Core/RuntimeService.h>
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
    int bindingCreateCount = 0;
    int bindingActivateCount = 0;
    int bindingCloseCount = 0;
    int bindingWaitCount = 0;
    bool rejectImports = false;
    bool returnWrongPayloadIdentity = false;

    class TestExports final : public srt::ContribExports {
    public:
        TestExports()
            : ContribExports(returnWrongPayloadIdentity ? "com.example.Wrong" : testInterface,
                             "test", 1) {
        }
    };

    class TestConfiguration final : public srt::ContribConfiguration {
    public:
        TestConfiguration() : ContribConfiguration(testInterface, "test", 1) {
        }
    };

    class TestOptions final : public srt::ContribImportOptions {
    public:
        TestOptions() : ContribImportOptions(testInterface, "test", 1) {
        }
    };

    class TestBinding final : public srt::ContribImportBinding {
    public:
        TestBinding(srt::ContribSpec &importer, const srt::ContribSpec::Import &declaration,
                    srt::ContribSpec &target, std::unique_ptr<srt::ContribImportOptions> options)
            : ContribImportBinding(importer, declaration, target, std::move(options)) {
        }

    private:
        void activate() noexcept override {
            ++bindingActivateCount;
        }

        void close() noexcept override {
            ++bindingCloseCount;
        }

        srt::Expected<void> wait() override {
            ++bindingWaitCount;
            return {};
        }
    };

    class TestExecInstance final : public srt::ContribExecInstance {
    public:
        explicit TestExecInstance(srt::ContribSpec &spec) : ContribExecInstance(spec) {
        }

    private:
        srt::Expected<void> quit() override {
            return {};
        }

        srt::Expected<void> wait() override {
            return {};
        }
    };

    class TestRuntimeService final : public srt::RuntimeService {
    public:
        TestRuntimeService(std::string iid, std::string name)
            : RuntimeService(std::move(iid), std::move(name)) {
        }
    };

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

        srt::Expected<std::unique_ptr<srt::ContribImportBinding>>
            createImportBinding(srt::ContribSpec &importer,
                                const srt::ContribSpec::Import &declaration,
                                srt::ContribSpec &target,
                                std::unique_ptr<srt::ContribImportOptions> options) const override {
            ++bindingCreateCount;
            return std::unique_ptr<srt::ContribImportBinding>(
                new TestBinding(importer, declaration, target, std::move(options)));
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
                                          R"(,"contributions":{")" + testCategoryName +
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

BOOST_AUTO_TEST_CASE(test_unregistered_runtime_service_is_movable) {
    TestRuntimeService original("org.openvpi.InferenceDriver", "onnx");
    TestRuntimeService moved(std::move(original));

    BOOST_CHECK_EQUAL(moved.iid(), "org.openvpi.InferenceDriver");
    BOOST_CHECK_EQUAL(moved.name(), "onnx");

    TestRuntimeService assigned("org.openvpi.Placeholder", "placeholder");
    assigned = std::move(moved);
    BOOST_CHECK_EQUAL(assigned.iid(), "org.openvpi.InferenceDriver");
    BOOST_CHECK_EQUAL(assigned.name(), "onnx");
}

BOOST_AUTO_TEST_CASE(test_runtime_services_are_owned_and_indexed_by_synth_unit) {
    TemporaryDirectory temporary;
    const auto root = writePackage(temporary.path(), "root", "root", "1");
    auto unit = makeUnit();

    auto onnx = std::make_unique<TestRuntimeService>("org.openvpi.InferenceDriver", "onnx");
    auto *onnxPointer = onnx.get();
    BOOST_REQUIRE(unit.addRuntimeService(std::move(onnx)));
    BOOST_REQUIRE(unit.addRuntimeService(
        std::make_unique<TestRuntimeService>("org.openvpi.InferenceDriver", "remote")));
    BOOST_CHECK(unit.runtimeService("org.openvpi.InferenceDriver", "onnx") == onnxPointer);
    BOOST_CHECK(&onnxPointer->synthUnit() == &unit);
    BOOST_CHECK_EQUAL(unit.runtimeServices("org.openvpi.InferenceDriver").size(), 2u);
    BOOST_CHECK(unit.runtimeServices("org.openvpi.Missing").empty());

    auto duplicate = unit.addRuntimeService(
        std::make_unique<TestRuntimeService>("org.openvpi.InferenceDriver", "onnx"));
    BOOST_REQUIRE(!duplicate);
    BOOST_CHECK(duplicate.error().code() == srt::Error::InvalidArgument);

    auto invalidIid =
        unit.addRuntimeService(std::make_unique<TestRuntimeService>("invalid iid", "test"));
    BOOST_REQUIRE(!invalidIid);
    BOOST_CHECK(invalidIid.error().code() == srt::Error::InvalidArgument);
    auto invalidName = unit.addRuntimeService(
        std::make_unique<TestRuntimeService>("org.openvpi.InferenceDriver", "invalid/name"));
    BOOST_REQUIRE(!invalidName);
    BOOST_CHECK(invalidName.error().code() == srt::Error::InvalidArgument);

    srt::SynthUnit moved(std::move(unit));
    BOOST_CHECK(&onnxPointer->synthUnit() == &moved);
    BOOST_CHECK(moved.runtimeService("org.openvpi.InferenceDriver", "onnx") == onnxPointer);

    BOOST_REQUIRE(moved.openPackage(root, srt::SynthUnit::DataOnly));
    auto late = moved.addRuntimeService(
        std::make_unique<TestRuntimeService>("org.openvpi.InferenceDriver", "late"));
    BOOST_REQUIRE(!late);
    BOOST_CHECK(late.error().code() == srt::Error::InvalidArgument);
}

BOOST_AUTO_TEST_CASE(test_data_only_reads_and_validates_dependencies) {
    TemporaryDirectory temporary;
    const auto valid = writePackage(temporary.path(), "valid", "root", "1", {},
                                    R"([{"id":"vendor/sample","version":"1.2.3"}])");

    auto unit = makeUnit();
    auto opened = unit.openPackage(valid, srt::SynthUnit::DataOnly);
    BOOST_REQUIRE(opened);
    BOOST_REQUIRE_EQUAL(opened->dependencies().size(), 1u);
    BOOST_CHECK_EQUAL(opened->dependencies()[0].id, "vendor/sample");
    BOOST_CHECK(opened->dependencies()[0].version == stdc::VersionNumber(1, 2, 3));

    std::size_t sequence = 0;
    for (const auto *dependencies : {
             R"([{"id":"sample","version":"1","required":true}])",
             R"([{"id":"sample","version":"01"}])",
             R"([{"id":"sample"}])",
             R"([{"version":"1"}])",
         }) {
        const auto invalid = writePackage(temporary.path(), "invalid-" + std::to_string(sequence++),
                                          "invalid", "1", {}, dependencies);
        auto result = unit.openPackage(invalid, srt::SynthUnit::DataOnly);
        BOOST_CHECK_MESSAGE(!result, dependencies);
    }
}

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
                  "applicationData":{"assetRoot":"${assets}"},
                  "contributions":{"com.example.test":[
                    {"id":"main","path":"${modulePath}"}
                  ]}
              })");
    writeText(root / "module.json",
              R"({
                  "interface":"com.example.Contract",
                  "variant":"test",
                  "level":1,
                  "vars":[
                    {"name":"d","value":"$"},
                    {"name":"dollars","value":"$$$$"}
                  ],
                  "name":"$${target}",
                  "extensionData":{"assetRoot":"${assets}"},
                  "exports":{},
                  "configuration":{
                    "assets":"${assets}",
                    "replacementIsNotRetokenized":"${d}{target}",
                    "replacementDollarsRemainLiteral":"${dollars}"
                  }
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
    BOOST_CHECK_EQUAL(package.manifestDeclaration().at("applicationData")["assetRoot"].toString(),
                      root.lexically_normal().string() + "/assets");
    BOOST_CHECK(package.manifestDeclaration().find("vars") == package.manifestDeclaration().end());

    auto *spec = package.contribution(testCategoryName, "main");
    BOOST_REQUIRE(spec);
    BOOST_CHECK_EQUAL(spec->name().text(), "${target}");
    BOOST_CHECK_EQUAL(spec->manifestDeclaration().at("extensionData")["assetRoot"].toString(),
                      root.lexically_normal().string() + "/assets");
    BOOST_CHECK(spec->manifestDeclaration().find("vars") == spec->manifestDeclaration().end());
    BOOST_CHECK_EQUAL(spec->manifestConfiguration()["assets"].toString(),
                      root.lexically_normal().string() + "/assets");
    BOOST_CHECK_EQUAL(spec->manifestConfiguration()["replacementIsNotRetokenized"].toString(),
                      "${target}");
    BOOST_CHECK_EQUAL(spec->manifestConfiguration()["replacementDollarsRemainLiteral"].toString(),
                      "$$");
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
    const auto oldBindingCreate = bindingCreateCount;
    const auto oldBindingActivate = bindingActivateCount;
    const auto oldBindingClose = bindingCloseCount;
    const auto oldBindingWait = bindingWaitCount;

    auto opened = unit.openPackage(root, srt::SynthUnit::Load);
    BOOST_REQUIRE(opened);
    auto package = opened.take();
    BOOST_CHECK(package.isLoaded());
    BOOST_CHECK_EQUAL(unit.loadedPackages().size(), 2u);

    auto *rootSpec = package.contribution(testCategoryName, "main");
    BOOST_REQUIRE(rootSpec);
    BOOST_CHECK(rootSpec->exports());
    BOOST_CHECK(rootSpec->configuration());
    BOOST_CHECK_EQUAL(rootSpec->exports()->interface(), testInterface);
    BOOST_CHECK_EQUAL(rootSpec->exports()->variant(), "test");
    BOOST_CHECK_EQUAL(rootSpec->exports()->level(), 1);
    BOOST_REQUIRE_EQUAL(rootSpec->imports().size(), 2u);
    BOOST_CHECK(rootSpec->imports()[0].options());
    BOOST_CHECK(rootSpec->imports()[1].options());
    BOOST_CHECK_EQUAL(rootSpec->imports()[0].options()->interface(), testInterface);
    BOOST_REQUIRE(rootSpec->imports()[0].binding());
    BOOST_REQUIRE(rootSpec->imports()[1].binding());
    BOOST_CHECK(rootSpec->imports()[0].binding()->state() ==
                srt::ContribImportBinding::State::Active);
    BOOST_CHECK(rootSpec->imports()[1].binding()->state() ==
                srt::ContribImportBinding::State::Active);

    const auto dependencyLocator = srt::ContribLocator::fromString("dep:com.example.test/main");
    auto *dependencySpec = package.resolve(dependencyLocator);
    BOOST_REQUIRE(dependencySpec);
    BOOST_CHECK(&rootSpec->imports()[0].binding()->importer() == rootSpec);
    BOOST_CHECK(&rootSpec->imports()[0].binding()->declaration() == &rootSpec->imports()[0]);
    BOOST_CHECK(&rootSpec->imports()[0].binding()->target() == dependencySpec);
    BOOST_CHECK(&rootSpec->imports()[0].binding()->options() == rootSpec->imports()[0].options());
    BOOST_CHECK(dependencySpec->package().version() == stdc::VersionNumber(2));
    {
        TestExecInstance instance(*rootSpec);
        BOOST_CHECK(&instance.spec() == rootSpec);
        BOOST_CHECK(instance.lifecycleState() == srt::ContribExecInstance::LifecycleState::Running);
    }
    BOOST_CHECK_EQUAL(exportsCount - oldExports, 2);
    BOOST_CHECK_EQUAL(configurationCount - oldConfiguration, 2);
    BOOST_CHECK_EQUAL(optionsCount - oldOptions, 2);
    BOOST_CHECK_EQUAL(validationCount - oldValidation, 2);
    BOOST_CHECK_EQUAL(bindingCreateCount - oldBindingCreate, 2);
    BOOST_CHECK_EQUAL(bindingActivateCount - oldBindingActivate, 2);

    auto openedAgain = unit.openPackage(root, srt::SynthUnit::Load);
    BOOST_REQUIRE(openedAgain);
    BOOST_CHECK(openedAgain.get() == package);
    BOOST_CHECK_EQUAL(exportsCount - oldExports, 2);
    BOOST_CHECK_EQUAL(bindingCreateCount - oldBindingCreate, 2);

    openedAgain->reset();
    package.reset();
    BOOST_CHECK_EQUAL(bindingCloseCount - oldBindingClose, 2);
    BOOST_CHECK_EQUAL(bindingWaitCount - oldBindingWait, 2);
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

BOOST_AUTO_TEST_CASE(test_missing_dependency_reports_not_found) {
    TemporaryDirectory temporary;
    const auto root = writePackage(temporary.path(), "root", "root", "1", {},
                                   R"([{"id":"missing","version":"1"}])");

    auto unit = makeUnit();
    auto opened = unit.openPackage(root, srt::SynthUnit::Load);

    BOOST_REQUIRE(!opened);
    BOOST_CHECK(opened.error().code() == srt::Error::FileNotFound);
    BOOST_CHECK(unit.loadedPackages().empty());
}

BOOST_AUTO_TEST_CASE(test_missing_import_target_reports_not_found) {
    TemporaryDirectory temporary;
    const auto root = writePackage(temporary.path(), "root", "root", "1", {}, "[]",
                                   R"([{"ref":":com.example.test/missing"}])");

    auto unit = makeUnit();
    auto opened = unit.openPackage(root, srt::SynthUnit::Load);

    BOOST_REQUIRE(!opened);
    BOOST_CHECK(opened.error().code() == srt::Error::FileNotFound);
    BOOST_CHECK(unit.loadedPackages().empty());
}

BOOST_AUTO_TEST_CASE(test_manifest_profile_distinguishes_invalid_and_extensible_input) {
    TemporaryDirectory temporary;
    const auto missingVersion = temporary.path() / "missing-version";
    writeText(missingVersion / "desc.json", R"({"id":"missing","version":"1","runtimeLevel":1})");

    auto unit = makeUnit();
    auto opened = unit.openPackage(missingVersion, srt::SynthUnit::DataOnly);
    BOOST_REQUIRE(!opened);
    BOOST_CHECK(opened.error().code() == srt::Error::InvalidFormat);

    const auto equivalentVersion = temporary.path() / "equivalent-version";
    writeText(equivalentVersion / "desc.json",
              R"({"$version":"1.0.0.0","id":"equivalent","version":"1","runtimeLevel":1})");
    opened = unit.openPackage(equivalentVersion, srt::SynthUnit::DataOnly);
    BOOST_REQUIRE(opened);

    const auto unsupportedVersion = temporary.path() / "unsupported-version";
    writeText(unsupportedVersion / "desc.json",
              R"({"$version":"2.0","id":"new","version":"1","runtimeLevel":1})");
    opened = unit.openPackage(unsupportedVersion, srt::SynthUnit::DataOnly);
    BOOST_REQUIRE(!opened);
    BOOST_CHECK(opened.error().code() == srt::Error::FeatureNotSupported);

    const auto duplicate = temporary.path() / "duplicate";
    writeText(duplicate / "desc.json",
              R"({"$version":"1.0","id":"first","id":"second","version":"1","runtimeLevel":1})");

    opened = unit.openPackage(duplicate, srt::SynthUnit::DataOnly);
    BOOST_REQUIRE(!opened);
    BOOST_CHECK(opened.error().code() == srt::Error::InvalidFormat);
    BOOST_CHECK(opened.error().toString().find("duplicate object key") != std::string::npos);

    const auto oldField = temporary.path() / "old-field";
    writeText(oldField / "desc.json",
              R"({"$version":"1.0","id":"old","version":"1","runtimeLevel":1,"contributes":{}})");
    opened = unit.openPackage(oldField, srt::SynthUnit::DataOnly);
    BOOST_REQUIRE(opened);
    BOOST_CHECK(opened->manifestDeclaration().at("contributes").isObject());

    const auto unknownCategory = temporary.path() / "unknown-category";
    writeText(
        unknownCategory / "desc.json",
        R"({"$version":"1.0","id":"unknown","version":"1","runtimeLevel":1,"contributions":{"com.example.unknown":[]}})");
    opened = unit.openPackage(unknownCategory, srt::SynthUnit::DataOnly);
    BOOST_REQUIRE(!opened);
    BOOST_CHECK(opened.error().code() == srt::Error::FeatureNotSupported);
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

BOOST_AUTO_TEST_CASE(test_wrong_payload_identity_rolls_back_package_state) {
    TemporaryDirectory temporary;
    const auto root = writePackage(temporary.path(), "root", "root", "1");

    auto unit = makeUnit();
    returnWrongPayloadIdentity = true;
    auto opened = unit.openPackage(root, srt::SynthUnit::Load);
    returnWrongPayloadIdentity = false;

    BOOST_REQUIRE(!opened);
    BOOST_CHECK(opened.error().code() == srt::Error::InvalidFormat);
    BOOST_CHECK(opened.error().toString().find("instead of") != std::string::npos);
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
                  "contributions":{"com.example.entry":[{"id":"one","value":"data"}]}
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
