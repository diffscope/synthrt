#ifndef SRT_G2P_PACKAGE_PACKAGE_P_H
#define SRT_G2P_PACKAGE_PACKAGE_P_H

#include <filesystem>
#include <map>
#include <string>

#include <stdcorelib/3rdparty/llvm/smallvector.h>
#include <stdcorelib/support/versionnumber.h>

#include <synthrt/Core/Module/Module.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Core/Support/JSON.h>

#include <synthrt/G2P/Package/Package.h>
#include <synthrt/G2P/Support/DisplayText.h>
#include <synthrt/G2P/Support/Error.h>

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
        explicit PackageData(PackageManager *mgr) : mgr(mgr) {}
        ~PackageData();

        /// Parse package.json from `dir` and populate fields.
        /// Phase 3.1: stubbed (returns NotImplementedError).
        srt::core::Expected<void> parse(
            const std::filesystem::path &dir,
            const std::map<std::string, srt::core::ModuleCategory *, std::less<>> &categories,
            llvm::SmallVectorImpl<srt::core::ModuleSpec *> *outModules);

        /// Read and parse package.json from `descPath` into a JsonObject.
        /// Implemented in P3.1 (used by PackageManager for package discovery).
        static srt::core::Expected<srt::core::JsonObject> readDesc(const std::filesystem::path &descPath);

        PackageManager *mgr;

        std::filesystem::path path;
        std::string id;

        stdc::VersionNumber version;
        stdc::VersionNumber compatVersion;

        DisplayText description;
        DisplayText vendor;
        DisplayText copyright;
        std::filesystem::path readme;
        std::string url;

        int level = 1;

        std::map<std::string, std::map<std::string, srt::core::ModuleSpec *, std::less<>>, std::less<>>
            moduleSpecs;

        Error err;
        bool loaded = false;
    };

} // namespace srt::g2p

#endif // SRT_G2P_PACKAGE_PACKAGE_P_H
