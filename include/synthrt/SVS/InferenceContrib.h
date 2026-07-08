#pragma once

#include <filesystem>
#include <string>

#include <synthrt/Core/Core/NamedObject.h>
#include <synthrt/Core/Module/Module.h>
#include <synthrt/Core/Support/DisplayText.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Core/Support/JSON.h>
#include <synthrt/Core/srt_core_global.h>
#include <synthrt/SVS/srt_svs_global.h>

namespace srt::svs {

    class Inference;
    class InferenceCategory;
    class InferenceInterpreter;

    class SRT_SVS_EXPORT InferenceInfoBase : public core::NamedObject {
    public:
        inline InferenceInfoBase(std::string name, std::string className, int apiLevel)
            : core::NamedObject(std::move(name)),
              _className(std::move(className)),
              _apiLevel(apiLevel) {
        }
        virtual ~InferenceInfoBase() = default;

        inline const std::string &className() const { return _className; }
        inline int apiLevel() const { return _apiLevel; }

    protected:
        std::string _className;
        int _apiLevel;
    };

    class InferenceSchema : public InferenceInfoBase {
    public:
        inline InferenceSchema(std::string name, std::string iid, int apiLevel)
            : InferenceInfoBase(std::move(name), std::move(iid), apiLevel) {}
    };

    class InferenceConfiguration : public InferenceInfoBase {
    public:
        inline InferenceConfiguration(std::string name, std::string iid, int apiLevel)
            : InferenceInfoBase(std::move(name), std::move(iid), apiLevel) {}
    };

    class InferenceImportOptions : public InferenceInfoBase {
    public:
        inline InferenceImportOptions(std::string name, std::string iid, int apiLevel)
            : InferenceInfoBase(std::move(name), std::move(iid), apiLevel) {}
    };

    class InferenceRuntimeOptions : public InferenceInfoBase {
    public:
        inline InferenceRuntimeOptions(std::string name, std::string iid, int apiLevel)
            : InferenceInfoBase(std::move(name), std::move(iid), apiLevel) {}
    };

    class SRT_SVS_EXPORT InferenceSpec : public core::ModuleSpec {
    public:
        ~InferenceSpec();

    public:
        const std::string &className() const;
        core::DisplayText name() const;
        int apiLevel() const;

        const core::JsonObject &manifestSchema() const;
        core::NO<InferenceSchema> schema() const;

        const core::JsonObject &manifestConfiguration() const;
        core::NO<InferenceConfiguration> configuration() const;

        const std::filesystem::path &path() const;

    public:
        core::Expected<core::NO<InferenceImportOptions>>
            createImportOptions(const core::JsonValue &options) const;
        core::Expected<core::NO<Inference>>
            createInference(const core::NO<InferenceImportOptions> &importOptions,
                            const core::NO<InferenceRuntimeOptions> &runtimeOptions) const;

    protected:
        class Impl;
        InferenceSpec();
        friend class InferenceCategory;
    };

    class SRT_SVS_EXPORT InferenceCategory : public core::ModuleCategory {
    public:
        ~InferenceCategory();

    public:
        std::vector<InferenceSpec *> findInferences(const core::ModuleLocator &identifier) const;
        std::vector<InferenceSpec *> inferences() const;

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
        explicit InferenceCategory(core::Runtime *su);
        friend class core::Runtime;
        friend class core::ModuleCategoryRegistrar<InferenceCategory>;
    };

}
