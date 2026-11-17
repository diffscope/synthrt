#define BOOST_TEST_MAIN
#include "TestSupport.h"

BOOST_AUTO_TEST_SUITE(test_SynthUnit)

BOOST_AUTO_TEST_CASE(test_runtime_relationship_nodes_are_not_movable) {
    BOOST_CHECK(!std::is_move_constructible_v<srt::SynthUnit>);
    BOOST_CHECK(!std::is_move_assignable_v<srt::SynthUnit>);
    BOOST_CHECK(!std::is_move_constructible_v<TestRuntimeService>);
    BOOST_CHECK(!std::is_move_assignable_v<TestRuntimeService>);
    BOOST_CHECK(!std::is_move_constructible_v<TestCategory>);
    BOOST_CHECK(!std::is_move_constructible_v<TestSpec>);
    BOOST_CHECK(!std::is_move_constructible_v<TestBinding>);
    BOOST_CHECK(!std::is_move_constructible_v<TestExtension>);
    BOOST_CHECK(!std::is_move_constructible_v<TestExecutive>);
    BOOST_CHECK(!std::is_move_constructible_v<TestInterpreter>);
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
    for (const auto dependencies : {
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
                  "license":"${assets}/LICENSE.txt",
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
    BOOST_CHECK_EQUAL(package.license().text(),
                      (root / "assets" / "LICENSE.txt").lexically_normal().string());
    BOOST_CHECK_EQUAL(package.manifestDeclaration().at("applicationData")["assetRoot"].toString(),
                      root.lexically_normal().string() + "/assets");
    BOOST_CHECK(package.manifestDeclaration().find("vars") == package.manifestDeclaration().end());

    auto spec = package.contribution(testCategoryName, "main");
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
    BOOST_CHECK(spec->extensions().empty());
    BOOST_CHECK(!spec->findExtension(testExtensionId));
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
        R"([{"role":"test/first","ref":"dep:com.example.test/main"},{"role":"test/second","ref":"dep:com.example.test/main","options":{}}])");

    auto unit = makeUnit();
    const std::vector<fs::path> paths = {firstPath, secondPath};
    unit.setPackagePaths(paths);
    const auto oldExports = exportsCount;
    const auto oldConfiguration = configurationCount;
    const auto oldOptions = optionsCount;
    const auto oldBindingCreate = bindingCreateCount;
    const auto oldBindingActivate = bindingActivateCount;
    const auto oldBindingClose = bindingCloseCount;
    const auto oldBindingWait = bindingWaitCount;
    const auto oldPreparedImportValidation = preparedImportValidationCount;
    const auto oldExtensionCreate = extensionCreateCount;
    validatorSawPreparedImports = false;

    auto opened = unit.openPackage(root, srt::SynthUnit::Load);
    BOOST_REQUIRE(opened);
    auto package = opened.take();
    BOOST_CHECK(package.isLoaded());
    BOOST_CHECK_EQUAL(unit.loadedPackages().size(), 2u);

    auto rootSpec = package.contribution(testCategoryName, "main");
    BOOST_REQUIRE(rootSpec);
    BOOST_CHECK(rootSpec->exports());
    BOOST_CHECK(rootSpec->configuration());
    BOOST_REQUIRE_EQUAL(rootSpec->extensions().size(), 2u);
    auto extension = rootSpec->findExtension(testExtensionId);
    BOOST_REQUIRE(extension);
    BOOST_CHECK(&extension->spec() == rootSpec);
    BOOST_CHECK_EQUAL(extension->id(), testExtensionId);
    BOOST_CHECK(rootSpec->findExtension(testSecondExtensionId) != nullptr);
    BOOST_CHECK(extension->as<TestExtension>() != nullptr);
    BOOST_CHECK(validatorSawPreparedImports);
    BOOST_CHECK_EQUAL(rootSpec->exports()->triplet().interfaceId(), testInterface);
    BOOST_CHECK_EQUAL(rootSpec->exports()->triplet().variant(), "test");
    BOOST_CHECK_EQUAL(rootSpec->exports()->triplet().level(), 1);
    BOOST_REQUIRE_EQUAL(rootSpec->imports().size(), 2u);
    BOOST_CHECK_EQUAL(rootSpec->imports()[0].role(), "test/first");
    BOOST_CHECK_EQUAL(rootSpec->imports()[1].role(), "test/second");
    const auto firstImport = rootSpec->findImport("test/first");
    BOOST_REQUIRE(firstImport);
    BOOST_CHECK_EQUAL(firstImport->role(), "test/first");
    BOOST_CHECK(firstImport->binding() == rootSpec->imports()[0].binding());
    BOOST_CHECK(!rootSpec->findImport("missing"));
    BOOST_CHECK(rootSpec->imports()[0].manifestOptions().isObject());
    BOOST_CHECK(rootSpec->imports()[0].manifestOptions().toObject().empty());
    BOOST_CHECK(rootSpec->imports()[1].manifestOptions().isObject());
    BOOST_CHECK(rootSpec->imports()[1].manifestOptions().toObject().empty());
    BOOST_CHECK(rootSpec->imports()[0].options());
    BOOST_CHECK(rootSpec->imports()[1].options());
    BOOST_CHECK_EQUAL(rootSpec->imports()[0].options()->triplet().interfaceId(), testInterface);
    BOOST_REQUIRE(rootSpec->imports()[0].binding());
    BOOST_REQUIRE(rootSpec->imports()[1].binding());
    BOOST_CHECK(rootSpec->imports()[0].binding()->state() ==
                srt::ContribImportBinding::State::Active);
    BOOST_CHECK(rootSpec->imports()[1].binding()->state() ==
                srt::ContribImportBinding::State::Active);

    const auto dependencyLocator = srt::ContribLocator::fromString("dep:com.example.test/main");
    auto dependencySpec = package.resolve(dependencyLocator);
    BOOST_REQUIRE(dependencySpec);
    BOOST_CHECK(&rootSpec->imports()[0].binding()->importer() == rootSpec);
    BOOST_CHECK(&rootSpec->imports()[0].binding()->declaration() == &rootSpec->imports()[0]);
    BOOST_CHECK(&rootSpec->imports()[0].binding()->target() == dependencySpec);
    BOOST_CHECK(&rootSpec->imports()[0].binding()->options() == rootSpec->imports()[0].options());
    BOOST_CHECK(dependencySpec->package().version() == stdc::VersionNumber(2));
    int childDestroyCount = 0;
    {
        TestExecutive parent(*rootSpec);
        BOOST_CHECK(&parent.spec() == rootSpec);
        BOOST_CHECK(&parent.synthUnit() == &unit);
        BOOST_CHECK(parent.lifecycleState() == srt::ContribExecutive::LifecycleState::Running);
        TestRuntimeOptions runtimeOptions;
        auto created = parent.create("test/first", runtimeOptions);
        BOOST_REQUIRE(created);
        BOOST_CHECK(&(*created)->spec() == dependencySpec);
        BOOST_CHECK((*created)->parent() == &parent);
        delete *created;
        BOOST_CHECK(parent.children().empty());
        {
            auto adopted = parent.adopt(std::make_unique<TestExecutive>(*dependencySpec));
            BOOST_REQUIRE(adopted);
            auto child = *adopted;
            BOOST_CHECK(child->parent() == &parent);
            BOOST_REQUIRE_EQUAL(parent.children().size(), 1u);
            BOOST_CHECK(parent.children().front() == child);

            delete child;
            BOOST_CHECK(parent.children().empty());
        }
        BOOST_REQUIRE(
            parent.adopt(std::make_unique<TestExecutive>(*dependencySpec, &childDestroyCount)));
        BOOST_CHECK_EQUAL(childDestroyCount, 0);
    }
    BOOST_CHECK_EQUAL(childDestroyCount, 1);
    BOOST_CHECK_EQUAL(exportsCount - oldExports, 2);
    BOOST_CHECK_EQUAL(configurationCount - oldConfiguration, 2);
    BOOST_CHECK_EQUAL(optionsCount - oldOptions, 2);
    BOOST_CHECK_EQUAL(bindingCreateCount - oldBindingCreate, 2);
    BOOST_CHECK_EQUAL(bindingActivateCount - oldBindingActivate, 2);
    BOOST_CHECK_EQUAL(preparedImportValidationCount - oldPreparedImportValidation, 2);
    BOOST_CHECK_EQUAL(extensionCreateCount - oldExtensionCreate, 2);

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

BOOST_AUTO_TEST_CASE(test_load_rejects_import_role_with_empty_segment) {
    TemporaryDirectory temporary;
    const auto root = writePackage(temporary.path(), "root", "root", "1", {}, {},
                                   R"([{"role":"test//invalid","ref":":com.example.test/main"}])");

    auto unit = makeUnit();
    auto opened = unit.openPackage(root, srt::SynthUnit::Load);

    BOOST_REQUIRE(!opened);
    BOOST_CHECK(opened.error().code() == srt::Error::InvalidFormat);
    BOOST_CHECK(unit.loadedPackages().empty());
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
                                   R"([{"role":"missing","ref":":com.example.test/missing"}])");

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

BOOST_AUTO_TEST_CASE(test_manifest_limits_json_nesting) {
    TemporaryDirectory temporary;
    const auto nestedArray = [](std::size_t depth) {
        return std::string(depth, '[') + "0" + std::string(depth, ']');
    };

    auto unit = makeUnit();
    const auto accepted = writePackage(temporary.path(), "accepted", "accepted", "1", {}, "[]",
                                       "[]", R"(,"probe":)" + nestedArray(400));
    auto opened = unit.openPackage(accepted, srt::SynthUnit::DataOnly);
    BOOST_REQUIRE(opened);

    const auto rejected = writePackage(temporary.path(), "rejected", "rejected", "1", {}, "[]",
                                       "[]", R"(,"probe":)" + nestedArray(100000));
    opened = unit.openPackage(rejected, srt::SynthUnit::DataOnly);
    BOOST_REQUIRE(!opened);
    BOOST_CHECK(opened.error().code() == srt::Error::InvalidFormat);
    BOOST_CHECK(opened.error().toString().find("nested too deeply") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(test_failed_prepared_import_validation_rolls_back_package_state) {
    TemporaryDirectory temporary;
    const auto root = writePackage(temporary.path(), "root", "root", "1");

    auto unit = makeUnit();
    rejectPreparedImports = true;
    auto opened = unit.openPackage(root, srt::SynthUnit::Load);
    rejectPreparedImports = false;

    BOOST_REQUIRE(!opened);
    BOOST_CHECK(opened.error().code() == srt::Error::InvalidFormat);
    BOOST_CHECK(opened.error().toString().find("prepared contribution imports") !=
                std::string::npos);
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

BOOST_AUTO_TEST_CASE(test_selected_dependency_failure_does_not_fall_back) {
    TemporaryDirectory temporary;
    const auto packages = temporary.path() / "packages";
    writePackage(packages, "dep-1", "dep", "1", "1");
    const auto broken = writePackage(packages, "dep-2", "dep", "2", "1");
    writeText(broken / "module.json",
              R"({"interface":"bad interface","level":1,"variant":"test"})");
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
    auto contribution = package.contribution("com.example.entry", "one");
    BOOST_REQUIRE(contribution);
    auto entry = contribution->as<EntrySpec>();
    BOOST_REQUIRE(entry);
    BOOST_CHECK_EQUAL(entry->value(), "data");
    BOOST_CHECK_EQUAL(unit.category("com.example.entry")->contributions().size(), 1u);

    package.reset();
    BOOST_CHECK(unit.loadedPackages().empty());
}

BOOST_AUTO_TEST_SUITE_END()
