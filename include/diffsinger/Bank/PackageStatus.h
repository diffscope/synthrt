#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <stdcorelib/support/versionnumber.h>

#include <synthrt/Core/Support/Diagnostic.h>
#include <diffsinger/Bank/dsbank_global.h>

namespace ds::bank {

    /// PackageStatus - Runtime status of a scanned package.
    ///
    /// \see 02-module-contracts.md section 5.3
    struct DSBANK_EXPORT PackageStatus {
        std::string packageId;
        stdc::VersionNumber version;
        std::filesystem::path rootPath;
        std::vector<std::string> dependencies;
        std::vector<std::string> unresolvedDependencies;
        bool valid = false;
        srt::core::Diagnostic error;
    };

} // namespace ds::bank
