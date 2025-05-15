#ifndef SYNTHRT_INFERENCECONTRIB_H
#define SYNTHRT_INFERENCECONTRIB_H

#include <synthrt/Core/Contribute.h>
#include <synthrt/Support/DisplayText.h>

namespace srt {

    /// InferenceInfoBase - The base class storing inference information which should be created
    /// by a specific inference interpreter.
    class InferenceInfoBase : public NamedObject {
    public:
        inline InferenceInfoBase(const std::string &name, const std::string &className, int apiLevel)
            : NamedObject(name), _className(className), _apiLevel(apiLevel) {
        }
        virtual ~InferenceInfoBase() = default;

        /// Related interpreter information.
        inline const std::string &className() const {
            return _className;
        }
        inline int apiLevel() const {
            return _apiLevel;
        }

    protected:
        std::string _className;
        int _apiLevel;
    };

    class InferenceSchema : public InferenceInfoBase {
    public:
        inline InferenceSchema(const std::string &name, const std::string &iid, int apiLevel)
            : InferenceInfoBase(name, iid, apiLevel) {
        }
    };

    class InferenceConfiguration : public InferenceInfoBase {
    public:
        inline InferenceConfiguration(const std::string &name, const std::string &iid, int apiLevel)
            : InferenceInfoBase(name, iid, apiLevel) {
        }
    };

    class InferenceImportOptions : public InferenceInfoBase {
    public:
        inline InferenceImportOptions(const std::string &name, const std::string &iid, int apiLevel)
            : InferenceInfoBase(name, iid, apiLevel) {
        }
    };

    class InferenceRuntimeOptions : public InferenceInfoBase {
    public:
        inline InferenceRuntimeOptions(const std::string &name, const std::string &iid,
                                       int apiLevel)
            : InferenceInfoBase(name, iid, apiLevel) {
        }
    };

    class Inference;

    class InferenceCategory;

    class SYNTHRT_EXPORT InferenceSpec : public ContribSpec {
    public:
        ~InferenceSpec();

    public:
        const std::string &className() const;
        DisplayText name() const;
        int apiLevel() const;

        const JsonObject &manifestSchema() const;
        NO<InferenceSchema> schema() const;

        const JsonObject &manifestConfiguration() const;
        NO<InferenceConfiguration> configuration() const;

        std::filesystem::path path() const;

    public:
        /// Mainly called by \a SingerSpec at loading state.
        NO<InferenceImportOptions> createImportOptions(const JsonValue &options,
                                                       Error *error) const;

        /// Creates an inference interface with the given options.
        NO<Inference> createInference(const NO<InferenceImportOptions> &importOptions,
                                      const NO<InferenceRuntimeOptions> &runtimeOptions,
                                      Error *error) const;

    protected:
        class Impl;
        InferenceSpec();

        friend class InferenceCategory;
    };

    class InferenceDriver;

    class SYNTHRT_EXPORT InferenceCategory : public ContribCategory {
    public:
        ~InferenceCategory();

    public:
        std::vector<InferenceSpec *> findInferences(const ContribLocator &identifier) const;
        std::vector<InferenceSpec *> inferences() const;

    protected:
        std::string key() const override;
        ContribSpec *parseSpec(const std::filesystem::path &basePath, const JsonValue &config,
                               Error *error) const override;
        bool loadSpec(ContribSpec *spec, ContribSpec::State state, Error *error) override;

    protected:
        class Impl;
        explicit InferenceCategory(SynthUnit *env);

        friend class SynthUnit;

        template <class InferenceCategory>
        friend class ContribCategoryRegistrar;
    };

}

#endif // SYNTHRT_INFERENCECONTRIB_H