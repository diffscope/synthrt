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

        // 按空格切分，跳过空 token，以容忍前导/连续/尾随空格（例如 DictStep
        // 历史版本会附加尾随空格，或调用方传入未规范化的字符串）。
        std::string_view::size_type start = 0;
        while (start <= pronunciation.size()) {
            const auto end = pronunciation.find(' ', start);
            if (end == std::string_view::npos) {
                auto token = pronunciation.substr(start);
                if (!token.empty()) {
                    result.emplace_back(token);
                }
                break;
            }

            auto token = pronunciation.substr(start, end - start);
            if (!token.empty()) {
                result.emplace_back(token);
            }
            start = end + 1;
        }

        return result;
    }

}
