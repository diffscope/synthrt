#include <diffsinger/Bank/SingerRef.h>

#include <string>
#include <string_view>
#include <utility>

namespace ds::bank {

    std::string SingerRef::toString() const {
        std::string result;
        result.reserve(packageId.size() + singerId.size() + 1);
        result.append(packageId);
        result.push_back(':');
        result.append(singerId);
        return result;
    }

    SingerRef SingerRef::parse(std::string_view s) {
        SingerRef ref;
        const auto pos = s.find(':');
        if (pos != std::string_view::npos) {
            ref.packageId = std::string(s.substr(0, pos));
            ref.singerId = std::string(s.substr(pos + 1));
        } else {
            // No separator: treat the whole string as the package id, leaving the
            // singer id empty. Callers validate the result against snapshots.
            ref.packageId = std::string(s);
        }
        return ref;
    }

} // namespace ds::bank
