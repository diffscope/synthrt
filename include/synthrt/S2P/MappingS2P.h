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

    /// MappingS2P converts a pronunciation by looking up each space-separated
    /// phoneme in a mapping table (original -> target).
    ///
    /// The mapping file is a TSV stream: each line is `original\ttarget`.
    /// `convert` is a `const` instance method because the mapping table is
    /// loaded once at construction.
    class SRT_S2P_EXPORT MappingS2P {
    public:
        /// Creates a MappingS2P from a TSV mapping stream.
        /// Returns an `Expected` error (InvalidFormat) on malformed input
        /// instead of throwing.
        static srt::core::Expected<std::unique_ptr<MappingS2P>> create(std::istream &mappingFile);

        ~MappingS2P();

        MappingS2P(const MappingS2P &) = delete;
        MappingS2P &operator=(const MappingS2P &) = delete;
        MappingS2P(MappingS2P &&) noexcept;
        MappingS2P &operator=(MappingS2P &&) noexcept;

        std::vector<std::string> convert(std::string_view pronunciation) const;

    private:
        MappingS2P();

        class Private;
        std::unique_ptr<Private> d;
    };

}
