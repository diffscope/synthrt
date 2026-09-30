#ifndef SYNTHRT_SINGERCONTRIB_H
#define SYNTHRT_SINGERCONTRIB_H

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <synthrt/Core/ContribCategory.h>
#include <synthrt/Core/ContribSpec.h>
#include <synthrt/Support/DisplayText.h>

namespace srt {

    /// The immutable declaration of one singer contribution.
    class SYNTHRT_EXPORT SingerSpec : public ContribSpec {
    public:
        ~SingerSpec();

        const DisplayText &avatar() const;
        const DisplayText &background() const;
        const DisplayText &demoAudio() const;

        /// Returns the languages declared by this singer, each mapped to the role of the import
        /// that provides the language. Returns an empty map if the singer declares no languages.
        ///
        /// The field belongs to the singer category rather than to any singer contract, because
        /// an editor lists the languages of every installed singer before it selects a provider,
        /// in the same way as the avatars. The category validates only the structure of the field
        /// and the existence of every role among the imports of this singer. The library that
        /// registers the language category validates the meaning of each handle and whether the
        /// referenced import is a language.
        const std::map<std::string, std::string> &languages() const;

        /// Returns the handle of the language used if a caller specifies no language. The handle
        /// is one of the keys of languages() if that map is not empty, and is empty otherwise.
        const std::string &defaultLanguage() const;

        /// Returns the reserved phonemes of this singer. A reserved phoneme is a phoneme token
        /// that a lyric may contain directly and that no language produces. Returns an empty list
        /// if the singer declares no reserved phonemes.
        ///
        /// The phoneme tables of the models of a singer contain two kinds of token. Most tokens
        /// result from phoneme conversion of a lyric. A few tokens represent non-speech sounds,
        /// such as a breath or a glottal stop, and reach the models because a user wrote the token
        /// in place of a lyric. The singer declares these tokens here, where every host and every
        /// language library reads them, so that no host or library passes such a token to phoneme
        /// conversion as if it were a word. The category validates only that the entries are
        /// distinct non-empty strings. The singer validator checks that the models contain them
        /// after the imports are prepared.
        const std::vector<std::string> &reservedPhonemes() const;

    private:
        SingerSpec(const ContribCreateContext &context, DisplayText avatar, DisplayText background,
                   DisplayText demoAudio, std::map<std::string, std::string> languages,
                   std::string defaultLanguage, std::vector<std::string> reservedPhonemes);

        DisplayText m_avatar;
        DisplayText m_background;
        DisplayText m_demoAudio;
        std::map<std::string, std::string> m_languages;
        std::string m_defaultLanguage;
        std::vector<std::string> m_reservedPhonemes;

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

#endif // SYNTHRT_SINGERCONTRIB_H
