#ifndef SYNTHRT_SINGERCONTRIB_H
#define SYNTHRT_SINGERCONTRIB_H

#include <filesystem>
#include <memory>
#include <vector>

#include <synthrt/Core/ContribCategory.h>
#include <synthrt/Core/ContribSpec.h>
#include <synthrt/Support/DisplayText.h>
#include <synthrt/synthrt_global.h>

namespace srt {

    /// The immutable declaration of one singer contribution.
    class SYNTHRT_EXPORT SingerSpec : public ContribSpec {
    public:
        ~SingerSpec();

        const std::filesystem::path &declarationPath() const;
        const DisplayText &avatar() const;
        const DisplayText &background() const;
        const DisplayText &demoAudio() const;

    private:
        SingerSpec(const ContribCreateContext &context, DisplayText avatar, DisplayText background,
                   DisplayText demoAudio);

        std::filesystem::path m_declarationPath;
        DisplayText m_avatar;
        DisplayText m_background;
        DisplayText m_demoAudio;

        friend class SingerCategory;
    };

    /// Parses and indexes contributions in the built in \c singer category.
    class SYNTHRT_EXPORT SingerCategory : public ContribCategory {
    public:
        SingerCategory();
        ~SingerCategory();

        std::vector<SingerSpec *> singers() const;

    protected:
        Expected<std::unique_ptr<ContribSpec>>
            createSpec(const ContribCreateContext &context) const override;
    };

}

#endif // SYNTHRT_SINGERCONTRIB_H
