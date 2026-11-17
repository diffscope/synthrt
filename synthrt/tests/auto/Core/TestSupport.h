#ifndef SYNTHRT_TESTS_CORE_TESTSUPPORT_H
#define SYNTHRT_TESTS_CORE_TESTSUPPORT_H

#include <system_error>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <stdcorelib/plugin/plugin.h>

#include <synthrt/Core/ContribCategory.h>
#include <synthrt/Core/ContribExecutive.h>
#include <synthrt/Core/ContribInterpreterPlugin.h>
#include <synthrt/Core/ContribSpec.h>
#include <synthrt/Core/ContribSpecExtension.h>
#include <synthrt/Core/PackageHandle.h>
#include <synthrt/Core/RuntimeService.h>
#include <synthrt/Core/SynthUnit.h>

#include <boost/test/unit_test.hpp>

namespace fs = std::filesystem;

namespace {

    constexpr const char *testCategoryName = "com.example.test";
    constexpr const char *testInterpreterIid = "com.example.TestInterpreter";
    constexpr const char *testInterface = "com.example.Contract";
    constexpr const char *testExtensionId = "com.example.TestExtension";
    constexpr const char *testSecondExtensionId = "com.example.SecondTestExtension";

    int pluginCreateCount = 0;
    int exportsCount = 0;
    int configurationCount = 0;
    int optionsCount = 0;
    int bindingCreateCount = 0;
    int bindingActivateCount = 0;
    int bindingCloseCount = 0;
    int bindingWaitCount = 0;
    bool returnWrongPayloadIdentity = false;
    int preparedImportValidationCount = 0;
    int extensionCreateCount = 0;
    bool rejectPreparedImports = false;
    bool validatorSawPreparedImports = false;

    class TestExports final : public srt::ContribExports {
    public:
        TestExports()
            : ContribExports(
                  {returnWrongPayloadIdentity ? "com.example.Wrong" : testInterface, 1, "test"}) {
        }
    };

    class TestConfiguration final : public srt::ContribConfiguration {
    public:
        TestConfiguration() : ContribConfiguration({testInterface, 1, "test"}) {
        }
    };

    class TestOptions final : public srt::ContribImportOptions {
    public:
        TestOptions() : ContribImportOptions({testInterface, 1, "test"}) {
        }
    };

    class TestRuntimeOptions final : public srt::ContribRuntimeOptions {
    public:
        TestRuntimeOptions() : ContribRuntimeOptions({testInterface, 1, "test"}) {
        }
    };

    class TestBinding final : public srt::ContribImportBinding {
    public:
        TestBinding(srt::ContribSpec &importer, const srt::ContribImport &declaration,
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

    class TestExecutive final : public srt::ContribExecutive {
    public:
        explicit TestExecutive(srt::ContribSpec &spec, int *destroyCount = nullptr)
            : ContribExecutive(spec), m_destroyCount(destroyCount) {
        }

        ~TestExecutive() {
            if (m_destroyCount) {
                ++*m_destroyCount;
            }
        }

        srt::Expected<TestExecutive *> adopt(std::unique_ptr<TestExecutive> child) {
            auto adopted = adoptChild(std::move(child));
            if (!adopted) {
                return adopted.takeError();
            }
            return static_cast<TestExecutive *>(*adopted);
        }

        srt::Expected<TestExecutive *> create(std::string_view role,
                                              const TestRuntimeOptions &runtimeOptions) {
            auto created = createChild(role, runtimeOptions);
            if (!created) {
                return created.takeError();
            }
            return static_cast<TestExecutive *>(*created);
        }

    private:
        srt::Expected<void> quit() override {
            return {};
        }

        srt::Expected<void> wait() override {
            return {};
        }

        int *m_destroyCount;
    };

    class TestExecutiveFactory final : public srt::ContribExecutiveFactory {
    public:
        explicit TestExecutiveFactory(srt::ContribImportBinding &binding) : m_binding(&binding) {
        }

        srt::Expected<std::unique_ptr<srt::ContribExecutive>>
            create(const srt::ContribRuntimeOptions &) override {
            return std::unique_ptr<srt::ContribExecutive>(new TestExecutive(m_binding->target()));
        }

    private:
        srt::ContribImportBinding *m_binding;
    };

    class TestRuntimeService final : public srt::RuntimeService {
    public:
        TestRuntimeService(std::string iid, std::string name)
            : RuntimeService(std::move(iid), std::move(name)) {
        }
    };

    class TestInterpreter final : public srt::ContribInterpreter {
    public:
        srt::Expected<std::vector<std::unique_ptr<srt::ContribImportValidator>>>
            createImportValidators() const override;

        srt::Expected<std::vector<std::unique_ptr<srt::ContribSpecExtension>>>
            createExtensions(srt::ContribSpec &spec) const override;

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

        srt::Expected<std::unique_ptr<srt::ContribImportBinding>>
            createImportBinding(srt::ContribSpec &importer, const srt::ContribImport &declaration,
                                srt::ContribSpec &target,
                                std::unique_ptr<srt::ContribImportOptions> options) const override {
            ++bindingCreateCount;
            return std::unique_ptr<srt::ContribImportBinding>(
                new TestBinding(importer, declaration, target, std::move(options)));
        }
    };

    class TestInterpreterPlugin final : public srt::ContribInterpreterPlugin {
    public:
        srt::Expected<std::unique_ptr<srt::ContribInterpreter>>
            create(const srt::ContribContractTriplet &triplet) override {
            if (triplet != srt::ContribContractTriplet(testInterface, 1, "test")) {
                return srt::Error(srt::Error::InvalidArgument,
                                  "unexpected test interpreter contract");
            }
            ++pluginCreateCount;
            return std::unique_ptr<srt::ContribInterpreter>(new TestInterpreter());
        }
    };

    class TestSpec final : public srt::ContribSpec {
    public:
        explicit TestSpec(const srt::ContribCreateContext &context) : ContribSpec(context) {
        }
    };

    class TestExtension final : public srt::ContribSpecExtension {
    public:
        TestExtension(srt::ContribSpec &spec, std::string id)
            : ContribSpecExtension(spec, std::move(id)) {
        }
    };

    class TestImportValidator final : public srt::ContribImportValidator {
    public:
        srt::Expected<void> validateImports(const srt::ContribSpec &spec) const override {
            if (spec.locator().category() != testCategoryName) {
                return {};
            }
            ++preparedImportValidationCount;
            for (const auto &item : spec.imports()) {
                if (!item.options() || !item.binding() || !item.executiveFactory()) {
                    return srt::Error(srt::Error::InvalidFormat,
                                      "test import was not fully prepared");
                }
                validatorSawPreparedImports = true;
            }
            if (rejectPreparedImports) {
                return srt::Error(srt::Error::InvalidFormat,
                                  "test validator rejected prepared imports");
            }
            return {};
        }
    };

    srt::Expected<std::vector<std::unique_ptr<srt::ContribImportValidator>>>
        TestInterpreter::createImportValidators() const {
        std::vector<std::unique_ptr<srt::ContribImportValidator>> result;
        result.emplace_back(new TestImportValidator());
        return result;
    }

    srt::Expected<std::vector<std::unique_ptr<srt::ContribSpecExtension>>>
        TestInterpreter::createExtensions(srt::ContribSpec &spec) const {
        std::vector<std::unique_ptr<srt::ContribSpecExtension>> result;
        if (spec.locator().category() != testCategoryName) {
            return result;
        }
        ++extensionCreateCount;
        result.emplace_back(new TestExtension(spec, testExtensionId));
        result.emplace_back(new TestExtension(spec, testSecondExtensionId));
        return result;
    }

    class TestCategory final : public srt::ContribCategory {
    public:
        TestCategory() : ContribCategory(testCategoryName, ModuleDeclaration, testInterpreterIid) {
        }

    protected:
        srt::Expected<std::unique_ptr<srt::ContribSpec>>
            createSpec(const srt::ContribCreateContext &context) const override {
            return std::unique_ptr<srt::ContribSpec>(new TestSpec(context));
        }

        srt::Expected<std::unique_ptr<srt::ContribExecutiveFactory>>
            createExecutiveFactory(srt::ContribImportBinding &binding) const override {
            return std::unique_ptr<srt::ContribExecutiveFactory>(new TestExecutiveFactory(binding));
        }
    };

    srt::ContribCategoryRegistry::Add<TestCategory> testCategoryRegistration(testCategoryName, "");

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
                                  {"name",         "test-interpreter"},
                                  {"interpreters",
                                   stdc::json::Array{
                                       stdc::json::Object{{"interface", testInterface},
                                                          {"level", 1},
                                                          {"variant", "test"}},
                                   }                                 },
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

    class ConfiguredSynthUnit final : public srt::SynthUnit {
    public:
        ConfiguredSynthUnit() {
            BOOST_REQUIRE(category(testCategoryName));
        }
    };

    ConfiguredSynthUnit makeUnit() {
        return {};
    }

}


#endif // SYNTHRT_TESTS_CORE_TESTSUPPORT_H
