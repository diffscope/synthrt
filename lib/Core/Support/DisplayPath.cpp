#include "DisplayPath.h"

#include <cctype>
#include <utility>

#include <stdcorelib/adt/vlarray.h>
#include <stdcorelib/path.h>
#include <stdcorelib/pimpl.h>

namespace srt::core {

    class DisplayPath::Impl {
    public:
        std::filesystem::path defaultPath;

        /// The language tags, owned here so that the map below has something to key into.
        ///
        /// \note Nothing may be added once \c paths refers into this. A language tag is short
        ///       enough to live in the string's own small buffer, so relocating one during a
        ///       regrow carries its characters along and leaves every key dangling.
        stdc::vlarray<std::string, 4> locales;

        /// Keyed by a view into \c locales rather than by a second copy of the same tag.
        std::map<std::string_view, std::filesystem::path, std::less<>> paths;
    };

    // Same matching rule as DisplayText (ds-spec 2.4): case-insensitive, the
    // '-' separator matched strictly.
    static bool tagEqualsIgnoreCase(std::string_view a, std::string_view b) {
        if (a.size() != b.size()) {
            return false;
        }
        for (size_t i = 0; i < a.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(a[i])) !=
                std::tolower(static_cast<unsigned char>(b[i]))) {
                return false;
            }
        }
        return true;
    }

    DisplayPath::DisplayPath() : _impl(std::make_shared<Impl>()) {
    }

    DisplayPath::DisplayPath(std::filesystem::path path) : _impl(std::make_shared<Impl>()) {
        stdc_impl_t;
        impl.defaultPath = std::move(path);
    }

    DisplayPath::DisplayPath(std::filesystem::path defaultPath,
                             const std::map<std::string, std::filesystem::path> &paths)
        : DisplayPath(std::move(defaultPath)) {
        stdc_impl_t;

        // Sized to its final length before a single view into it is taken, so that no push below
        // can relocate what an earlier key already points at.
        impl.locales.reserve(paths.size());
        for (const auto &item : paths) {
            impl.locales.push_back(item.first);
            impl.paths.emplace(impl.locales.back(), item.second);
        }
    }

    DisplayPath::~DisplayPath() = default;

    DisplayPath &DisplayPath::operator=(std::filesystem::path path) {
        stdc_impl_t;
        impl.defaultPath = std::move(path);
        return *this;
    }

    Expected<DisplayPath> DisplayPath::fromJsonValue(const JsonValue &value) {
        if (value.isString()) {
            return DisplayPath(stdc::path::from_utf8(value.toString()));
        }

        if (!value.isObject()) {
            return Error{
                Error::InvalidFormat,
                R"(must be a string or an object)",
            };
        }

        const auto &obj = value.toObject();
        auto itDefault = obj.find("_");
        if (itDefault == obj.end()) {
            return Error{
                Error::InvalidFormat,
                R"(must contain "_" field)",
            };
        }
        if (!itDefault->second.isString()) {
            return Error{
                Error::InvalidFormat,
                R"("_" field must be a string)",
            };
        }

        std::map<std::string, std::filesystem::path> paths;
        for (const auto &item : obj) {
            if (!item.second.isString()) {
                return Error{
                    Error::InvalidFormat,
                    R"(field ")" + item.first + R"(" must be a string)",
                };
            }
            if (item.first != "_") {
                paths[item.first] = stdc::path::from_utf8(item.second.toString());
            }
        }

        return DisplayPath(stdc::path::from_utf8(itDefault->second.toString()), paths);
    }

    void DisplayPath::swap(DisplayPath &RHS) noexcept {
        _impl.swap(RHS._impl);
    }

    const std::filesystem::path &DisplayPath::path() const {
        stdc_impl_t;
        return impl.defaultPath;
    }

    const std::filesystem::path &DisplayPath::path(std::string_view locale) const {
        stdc_impl_t;

        // RFC 4647 §3.4 Lookup, identical to DisplayText::text(locale).
        std::string_view range = locale;
        while (!range.empty()) {
            for (const auto &item : impl.paths) {
                if (tagEqualsIgnoreCase(item.first, range)) {
                    return item.second;
                }
            }
            const auto pos = range.find_last_of('-');
            if (pos == std::string_view::npos) {
                break;
            }
            range = range.substr(0, pos);
        }
        return impl.defaultPath;
    }

    bool DisplayPath::isEmpty() const {
        stdc_impl_t;
        return impl.defaultPath.empty();
    }

} // namespace srt::core
