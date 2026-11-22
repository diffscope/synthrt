#ifndef SYNTHRT_SVS_SINGERCONTRIB_H
#define SYNTHRT_SVS_SINGERCONTRIB_H

#include <memory>
#include <vector>

#include <synthrt/Core/ContribCategory.h>
#include <synthrt/Core/ContribSpec.h>
#include <synthrt/Support/DisplayText.h>

namespace srt {

    /// The immutable declaration of one singer contribution.
    class SYNTHRT_EXPORT SingerSpec : public ContribSpec {
    public:
        ~SingerSpec();

        /// Returns the avatar path declared by this singer.
        const DisplayText &avatar() const;

        /// Returns the background path declared by this singer.
        const DisplayText &background() const;

        /// Returns the demo audio path declared by this singer.
        const DisplayText &demoAudio() const;

    private:
        SingerSpec(const ContribCreateContext &context, DisplayText avatar, DisplayText background,
                   DisplayText demoAudio);

        DisplayText m_avatar;
        DisplayText m_background;
        DisplayText m_demoAudio;

        friend class SingerCategory;
    };

    /// Parses and indexes contributions in the built in \c singer category.
    class SYNTHRT_EXPORT SingerCategory : public ContribCategory {
    public:
        /// Name of the category, as passed by a host to \c SynthUnit::setPluginPaths and as used
        /// in a \c ModuleReference.
        static constexpr const char *NAME = "singer";

        SingerCategory();
        ~SingerCategory();

        std::vector<SingerSpec *> singers() const;

    protected:
        Expected<std::unique_ptr<ContribSpec>>
            createSpec(const ContribCreateContext &context) const override;
    };

}

#endif // SYNTHRT_SVS_SINGERCONTRIB_H
