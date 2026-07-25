#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>

#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Core/Support/JSON.h>
#include <synthrt/Core/srt_core_global.h>

namespace srt::core {

    class SRT_CORE_EXPORT DisplayPath {
    public:
        DisplayPath();
        DisplayPath(std::filesystem::path path);
        DisplayPath(std::filesystem::path defaultPath,
                    const std::map<std::string, std::filesystem::path> &paths);
        ~DisplayPath();

        DisplayPath &operator=(std::filesystem::path path);

        static Expected<DisplayPath> fromJsonValue(const JsonValue &value);

        void swap(DisplayPath &RHS) noexcept;

    public:
        const std::filesystem::path &path() const;
        const std::filesystem::path &path(std::string_view locale) const;

        bool isEmpty() const;

    protected:
        class Impl;
        std::shared_ptr<Impl> _impl;
    };

}
