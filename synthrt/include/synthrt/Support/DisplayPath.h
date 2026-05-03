#ifndef SYNTHRT_DISPLAYPATH_H
#define SYNTHRT_DISPLAYPATH_H

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>

#include <synthrt/Support/Expected.h>
#include <synthrt/Support/JSON.h>

namespace srt {

    /// DisplayPath - Represents a path with multiple translations.
    class SYNTHRT_EXPORT DisplayPath {
    public:
        /// Constructs an empty display path object.
        DisplayPath();

        /// Constructs with a default path.
        DisplayPath(std::filesystem::path path);

        /// Constructs with a default path and a map, where the key is the locale code and the value
        /// is the corresponding path.
        DisplayPath(std::filesystem::path defaultPath,
                    const std::map<std::string, std::filesystem::path> &paths);

        ~DisplayPath();

        DisplayPath &operator=(std::filesystem::path path);

        static Expected<DisplayPath> fromJsonValue(const JsonValue &value);

        inline void swap(DisplayPath &RHS) noexcept {
            _impl.swap(RHS._impl);
        }

    public:
        const std::filesystem::path &path() const;
        const std::filesystem::path &path(std::string_view locale) const;

        bool isEmpty() const;

    protected:
        class Impl;
        std::shared_ptr<Impl> _impl;
    };

}

#endif // SYNTHRT_DISPLAYPATH_H
