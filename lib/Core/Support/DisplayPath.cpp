#include "DisplayPath.h"

#include <utility>

#include <stdcorelib/adt/vlarray.h>
#include <stdcorelib/path.h>
#include <stdcorelib/pimpl.h>

namespace srt::core {

    class DisplayPath::Impl {
    public:
        std::filesystem::path defaultPath;

        /// The language tags, owned here so that \c locales() has something to hand out and the
        /// map below has something to key into.
        ///
        /// \note Nothing may be added once \c paths refers into this. A language tag is short
        ///       enough to live in the string's own small buffer, so relocating one during a
        ///       regrow carries its characters along and leaves every key dangling.
        stdc::vlarray<std::string, 4> locales;

        /// Keyed by a view into \c locales rather than by a second copy of the same tag.
        std::map<std::string_view, std::filesystem::path, std::less<>> paths;
    };

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
            // "_" is the default entry of the JSON object form, not a
            // translation key: keep it out of locales()/path(key) even if a
            // direct caller passes it in the map.
            if (item.first == "_") {
                continue;
            }
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

    const std::filesystem::path *DisplayPath::path(std::string_view key) const {
        stdc_impl_t;

        // Same pass-through semantics as DisplayText::text(key): exact,
        // case-sensitive key lookup, no matching, no fallback.
        const auto it = impl.paths.find(key);
        if (it == impl.paths.end()) {
            return nullptr;
        }
        return &it->second;
    }

    stdc::array_view<std::string> DisplayPath::locales() const {
        stdc_impl_t;
        return {impl.locales.data(), impl.locales.size()};
    }

    bool DisplayPath::isEmpty() const {
        stdc_impl_t;
        return impl.defaultPath.empty();
    }

} // namespace srt::core
