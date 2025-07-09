#ifndef SYNTHRT_SINGERCONTRIB_H
#define SYNTHRT_SINGERCONTRIB_H

#include <synthrt/Support/DisplayText.h>
#include <synthrt/Core/Contribute.h>
#include <synthrt/SVS/InferenceContrib.h>

namespace srt {

    class SingerSpec;

    class SingerCategory;

    class SingerImportData;

    /// SingerInfoBase - The base class storing singer information.
    class SingerInfoBase : public NamedObject {
    public:
        inline SingerInfoBase(std::string name, int apiLevel)
            : NamedObject(std::move(name)), _apiLevel(apiLevel) {
        }
        virtual ~SingerInfoBase() = default;

        inline int apiLevel() const {
            return _apiLevel;
        }

    protected:
        int _apiLevel;
    };

    class SingerConfiguration : public SingerInfoBase {
    public:
        inline SingerConfiguration(std::string model, int apiLevel)
            : SingerInfoBase(std::move(model), apiLevel) {
        }
    };

    class SYNTHRT_EXPORT SingerImport {
    public:
        SingerImport();
        ~SingerImport();

        bool isNull() const;

        /// The locator of the imported inference.
        const ContribLocator &inferenceLocator() const;

        /// The related \a InferenceSpec instance.
        InferenceSpec *inference() const;

        /// The format of options is determined by the singer model and inference kind.
        JsonValue manifestOptions() const;

        /// The \a SingerImportOptions instance created by the related interpreter.
        NO<InferenceImportOptions> options() const;

    protected:
        SingerImport(const SingerImportData *data);

        const SingerImportData *_data;

        friend class SingerSpec;
        friend class SingerCategory;
    };

    class SYNTHRT_EXPORT SingerSpec : public ContribSpec {
    public:
        ~SingerSpec();

    public:
        /// The \a arch attribute indicates the engine to which the singer's voice library belongs
        const std::string &arch() const;
        DisplayText name() const;
        int apiLevel() const;

        const std::filesystem::path &avatar() const;
        const std::filesystem::path &background() const;
        const std::filesystem::path &demoAudio() const;

        stdc::array_view<SingerImport> imports() const;

        const JsonObject &manifestConfiguration() const;
        NO<SingerConfiguration> configuration() const;

        const std::filesystem::path &path() const;

    protected:
        class Impl;
        SingerSpec();

        friend class SingerCategory;
    };

    class SYNTHRT_EXPORT SingerCategory : public ContribCategory {
    public:
        ~SingerCategory();

    public:
        std::vector<SingerSpec *> findSingers(const ContribLocator &locator) const;
        std::vector<SingerSpec *> singers() const;

    protected:
        std::string key() const override;
        Expected<ContribSpec *> parseSpec(const std::filesystem::path &basePath,
                                          const JsonValue &config) const override;
        Expected<void> loadSpec(ContribSpec *spec, ContribSpec::State state) override;

    protected:
        class Impl;
        explicit SingerCategory(SynthUnit *su);

        friend class SynthUnit;
        friend class ContribCategoryRegistrar<SingerCategory>;
    };

}

#endif // SYNTHRT_SINGERCONTRIB_H