#pragma once

#include <stdcorelib/support/versionnumber.h>
#include <synthrt/Core/Module/Module.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Core/Support/JSON.h>
#include <synthrt/G2P/Package/Package.h>
#include <synthrt/G2P/Support/DisplayText.h>
#include <synthrt/G2P/Support/Error.h>

#include <filesystem>
#include <map>
#include <string>

namespace srt::g2p {

    /// PackageData - private data wrapped by the Package handle.
    ///
    /// Migrated from LangCore::PackageData. Owned by PackageManager (created
    /// by open(), deleted by close()). Stores parsed package manifest fields
    /// and contributed ModuleSpec instances.
    ///
    /// Phase 3.1 scope: basic data fields and a stubbed parse() that returns
    /// NotImplementedError; full manifest parsing lands in P3.2.
    class PackageData {
    public:
        explicit PackageData(PackageManager *mgr) : m_mgr(mgr) {
        }
        ~PackageData();

        /// Parse package.json from `dir` and populate fields.
        /// Phase 3.1: stubbed (returns NotImplementedError).
        srt::core::Expected<void>
            parse(const std::filesystem::path                                           &dir,
                  const std::map<std::string, srt::core::ModuleCategory *, std::less<>> &categories,
                  std::vector<srt::core::ModuleSpec *>                                  *outModules);

        /// Read and parse package.json from `descPath` into a JsonObject.
        /// Implemented in P3.1 (used by PackageManager for package discovery).
        static srt::core::Expected<srt::core::JsonObject> readDesc(const std::filesystem::path &descPath);

        PackageManager *m_mgr;

        std::filesystem::path m_path;
        std::string           m_id;

        stdc::VersionNumber m_version;
        stdc::VersionNumber m_compatVersion;

        DisplayText           m_description;
        DisplayText           m_vendor;
        DisplayText           m_copyright;
        std::filesystem::path m_readme;
        std::string           m_url;

        int m_level = 1;

        std::map<std::string, std::map<std::string, srt::core::ModuleSpec *, std::less<>>, std::less<>> m_moduleSpecs;

        Error m_err;
        bool  m_loaded = false;
    };

} // namespace srt::g2p
