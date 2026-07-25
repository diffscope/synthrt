#pragma once

#include <iosfwd>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <synthrt/Core/Support/Error.h>
#include <synthrt/Core/Support/Expected.h>

#include <synthrt/S2P/srt_s2p_global.h>

namespace srt::s2p {

    /// DictionaryS2P looks up a whole pronunciation in a dictionary and
    /// returns the precomputed phoneme sequence.
    ///
    /// The dictionary file is a TSV stream: each line is
    /// `pronunciation\tphoneme1 phoneme2 ...`. `convert` is a `const` instance
    /// method because the dictionary is loaded once at construction. When the
    /// pronunciation is not found, an empty vector is returned.
    class SRT_S2P_EXPORT DictionaryS2P {
    public:
        /// Creates a DictionaryS2P from a TSV dictionary stream.
        /// Returns an `Expected` error (InvalidFormat) on malformed input
        /// instead of throwing.
        static srt::core::Expected<std::unique_ptr<DictionaryS2P>> create(std::istream &dictionaryFile);

        ~DictionaryS2P();

        DictionaryS2P(const DictionaryS2P &) = delete;
        DictionaryS2P &operator=(const DictionaryS2P &) = delete;
        DictionaryS2P(DictionaryS2P &&) noexcept;
        DictionaryS2P &operator=(DictionaryS2P &&) noexcept;

        std::vector<std::string> convert(std::string_view pronunciation) const;

    private:
        DictionaryS2P();

        class Private;
        std::unique_ptr<Private> d;
    };

}
