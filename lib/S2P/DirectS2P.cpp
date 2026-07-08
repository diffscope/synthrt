#include <synthrt/S2P/DirectS2P.h>

#include <memory>

namespace srt::s2p {

    class DirectS2P::Private {
    };

    DirectS2P::DirectS2P() : d(std::make_unique<Private>()) {
    }

    DirectS2P::~DirectS2P() = default;

    DirectS2P::DirectS2P(DirectS2P &&) noexcept = default;

    DirectS2P &DirectS2P::operator=(DirectS2P &&) noexcept = default;

    std::vector<std::string> DirectS2P::convert(std::string_view pronunciation) {
        std::vector<std::string> result;

        std::string_view::size_type start = 0;
        while (true) {
            const auto end = pronunciation.find(' ', start);
            if (end == std::string_view::npos) {
                result.emplace_back(pronunciation.substr(start));
                break;
            }

            result.emplace_back(pronunciation.substr(start, end - start));
            start = end + 1;
        }

        return result;
    }

}
