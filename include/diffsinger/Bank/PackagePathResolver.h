#pragma once

#include <filesystem>
#include <string_view>

#include <synthrt/Core/Support/Expected.h>

#include <diffsinger/Bank/dsbank_global.h>

namespace ds::bank {

    /// Resolves a package resource reference without allowing it to leave the
    /// package root. Absolute paths and paths escaping through symlinks or
    /// junctions are rejected.
    class DSBANK_EXPORT PackagePathResolver {
    public:
        /// Resolves \p reference relative to \p baseDir. Both \p baseDir and
        /// the resolved path must be within \p packageRoot.
        static srt::core::Expected<std::filesystem::path>
            resolve(const std::filesystem::path &packageRoot,
                    const std::filesystem::path &baseDir,
                    std::string_view reference);
    };

}
