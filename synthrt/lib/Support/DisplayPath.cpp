#include "DisplayPath.h"

#include <optional>
#include <utility>

#include <stdcorelib/path.h>
#include <stdcorelib/pimpl.h>

namespace srt {

    class DisplayPath::Impl {
    public:
        std::filesystem::path defaultPath;
        std::optional<std::map<std::string, std::filesystem::path, std::less<>>> paths;

        void clear() {
            defaultPath.clear();
            paths.reset();
        }
    };

    DisplayPath::DisplayPath() : _impl(std::make_shared<Impl>()) {
    }

    DisplayPath::DisplayPath(std::filesystem::path path) : _impl(std::make_shared<Impl>()) {
        __stdc_impl_t;
        impl.defaultPath = std::move(path);
    }

    DisplayPath::DisplayPath(std::filesystem::path defaultPath,
                             const std::map<std::string, std::filesystem::path> &paths)
        : DisplayPath(std::move(defaultPath)) {
        __stdc_impl_t;
        impl.paths = {paths.begin(), paths.end()};
    }

    DisplayPath::~DisplayPath() = default;

    DisplayPath &DisplayPath::operator=(std::filesystem::path path) {
        __stdc_impl_t;
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

    const std::filesystem::path &DisplayPath::path() const {
        __stdc_impl_t;
        return impl.defaultPath;
    }

    const std::filesystem::path &DisplayPath::path(std::string_view locale) const {
        __stdc_impl_t;
        if (!impl.paths) {
            return impl.defaultPath;
        }
        auto it = impl.paths->find(locale);
        if (it == impl.paths->end()) {
            return impl.defaultPath;
        }
        return it->second;
    }

    bool DisplayPath::isEmpty() const {
        __stdc_impl_t;
        return impl.defaultPath.empty();
    }

}
