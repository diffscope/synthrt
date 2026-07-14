#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <synthrt/Core/Core/NamedObject.h>
#include <synthrt/Core/Module/Module.h>
#include <synthrt/Core/Support/DisplayText.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Core/Support/JSON.h>
#include <synthrt/SVS/InferenceContrib.h>
#include <synthrt/Core/srt_core_global.h>
#include <synthrt/SVS/srt_svs_global.h>

namespace srt::svs {

    class SingerCategory;
    class SingerSpec;
    class InferenceSpec;

    class SRT_SVS_EXPORT SingerImport {
    public:
        SingerImport();
        ~SingerImport();

        bool isNull() const;
        InferenceSpec *inference() const;
        core::JsonValue manifestOptions() const;
        core::NO<InferenceImportOptions> options() const;

    protected:
        InferenceSpec *_inference = nullptr;
        core::JsonValue _manifestOptions;
        core::NO<InferenceImportOptions> _options = nullptr;
        // Cross-package declaration: when _declaredPackage is empty, the import
        // resolves within the singer's own package (strict isolation). When
        // non-empty, the import resolves across packages by matching the
        // declared package id. _declaredVersion supports "*" or empty (any
        // version) or a specific version string (exact match).
        std::string _declaredPackage;
        std::string _declaredVersion;
        friend class SingerSpec;
        friend class SingerCategory;
    };

    class SRT_SVS_EXPORT SingerInfoBase : public core::NamedObject {
    public:
        inline SingerInfoBase(std::string name, int apiLevel)
            : core::NamedObject(std::move(name)), _apiLevel(apiLevel) {}
        virtual ~SingerInfoBase() = default;
        inline int apiLevel() const { return _apiLevel; }

    protected:
        int _apiLevel;
    };

    class SingerConfiguration : public SingerInfoBase {
    public:
        inline SingerConfiguration(std::string model, int apiLevel)
            : SingerInfoBase(std::move(model), apiLevel) {}
    };

    class SRT_SVS_EXPORT SingerSpec : public core::ModuleSpec {
    public:
        ~SingerSpec();

    public:
        const std::string &className() const;
        core::DisplayText name() const;
        int apiLevel() const;

        const core::JsonObject &manifestConfiguration() const;
        core::NO<SingerConfiguration> configuration() const;
        const std::vector<SingerImport> &imports() const;

        const std::filesystem::path &path() const;

    protected:
        class Impl;
        SingerSpec();
        friend class SingerCategory;
    };

    class SRT_SVS_EXPORT SingerCategory : public core::ModuleCategory {
    public:
        ~SingerCategory();

    public:
        std::vector<SingerSpec *> findSingers(const core::ModuleLocator &locator) const;
        std::vector<SingerSpec *> singers() const;

    protected:
        std::string key() const override;
        std::string category() const override;
        core::Expected<core::ModuleSpec *>
            parseSpec(const std::filesystem::path &basePath,
                      const core::JsonValue &config) const override;
        core::Expected<void> loadSpec(core::ModuleSpec *spec,
                                      core::ModuleSpec::State state) override;

    protected:
        class Impl;
        explicit SingerCategory(core::Runtime *su);
        friend class core::Runtime;
        friend class core::ModuleCategoryRegistrar<SingerCategory>;
    };

}
