#ifndef SYNTHRT_SINGERCONTRIB_H
#define SYNTHRT_SINGERCONTRIB_H

#include <synthrt/Support/DisplayText.h>
#include <synthrt/Core/Contribute.h>
#include <synthrt/SVS/InferenceContrib.h>

namespace srt {

    class SingerSpec;

    class SingerCategory;

    class SingerImportData;

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
        /// The \a model attribute indicates the engine to which the singer's voice library belongs
        const std::string &model() const;

        /// Author information, for display purposes only.
        DisplayText name() const;
        std::filesystem::path avatar() const;
        std::filesystem::path background() const;
        std::filesystem::path demoAudio() const;

        std::filesystem::path path() const;

        stdc::array_view<SingerImport> imports() const;
        const JsonObject &configuration() const; // Misc resources

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
        Expected<bool> loadSpec(ContribSpec *spec, ContribSpec::State state) override;

    protected:
        class Impl;
        explicit SingerCategory(SynthUnit *su);

        friend class SynthUnit;

        template <class T>
        friend class ContribCategoryRegistrar;
    };

}

#endif // SYNTHRT_SINGERCONTRIB_H