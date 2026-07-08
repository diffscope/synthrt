#ifndef SRT_S2P_DIRECTS2P_H
#define SRT_S2P_DIRECTS2P_H

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <synthrt/S2P/srt_s2p_global.h>

namespace srt::s2p {

    /// DirectS2P splits a pronunciation string by spaces into a phoneme list.
    ///
    /// It is the simplest S2P converter: no mapping, no dictionary, no Lua. The
    /// `convert` method is `static` because the converter holds no state.
    class SRT_S2P_EXPORT DirectS2P {
    public:
        DirectS2P();
        ~DirectS2P();

        DirectS2P(const DirectS2P &) = delete;
        DirectS2P &operator=(const DirectS2P &) = delete;
        DirectS2P(DirectS2P &&) noexcept;
        DirectS2P &operator=(DirectS2P &&) noexcept;

        /// Splits @p pronunciation by ASCII space into a list of phonemes.
        static std::vector<std::string> convert(std::string_view pronunciation);

    private:
        class Private;
        std::unique_ptr<Private> d;
    };

}

#endif // SRT_S2P_DIRECTS2P_H
