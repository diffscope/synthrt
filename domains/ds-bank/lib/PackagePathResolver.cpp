#include <system_error>

#include <stdcorelib/str.h>

#include <synthrt/Core/Support/Error.h>

#include <diffsinger/Bank/PackagePathResolver.h>

namespace ds::bank {

    namespace {
        bool isWithin(const std::filesystem::path &root, const std::filesystem::path &path) {
            auto rootIt = root.begin();
            auto pathIt = path.begin();
            for (; rootIt != root.end() && pathIt != path.end(); ++rootIt, ++pathIt) {
                if (*rootIt != *pathIt) {
                    return false;
                }
            }
            return rootIt == root.end();
        }

        srt::core::Error invalidReference(std::string_view reference, const char *reason) {
            return {srt::core::ErrorCode::PackageManifestInvalid,
                    stdc::formatN(R"(%1: invalid package resource path (%2))", reference, reason)};
        }
    }

    srt::core::Expected<std::filesystem::path>
        PackagePathResolver::resolve(const std::filesystem::path &packageRoot,
                                     const std::filesystem::path &baseDir,
                                     std::string_view reference) {
        if (reference.empty()) {
            return invalidReference(reference, "empty path");
        }

        const std::filesystem::path relativePath{std::string(reference)};
        if (relativePath.is_absolute() || relativePath.has_root_name() || relativePath.has_root_directory()) {
            return invalidReference(reference, "absolute paths are not allowed");
        }

        std::error_code ec;
        const auto lexicalRoot = std::filesystem::absolute(packageRoot, ec).lexically_normal();
        if (ec) {
            return invalidReference(reference, "cannot normalize package root");
        }
        const auto lexicalBase = std::filesystem::absolute(baseDir, ec).lexically_normal();
        if (ec || !isWithin(lexicalRoot, lexicalBase)) {
            return invalidReference(reference, "base directory is outside package root");
        }
        const auto lexicalTarget = (lexicalBase / relativePath).lexically_normal();
        if (!isWithin(lexicalRoot, lexicalTarget)) {
            return invalidReference(reference, "path escapes package root");
        }

        const auto canonicalRoot = std::filesystem::weakly_canonical(lexicalRoot, ec);
        if (ec) {
            return invalidReference(reference, "cannot resolve package root");
        }
        const auto canonicalTarget = std::filesystem::weakly_canonical(lexicalTarget, ec);
        if (ec || !isWithin(canonicalRoot, canonicalTarget)) {
            return invalidReference(reference, "path escapes package root through a symlink or junction");
        }
        return canonicalTarget;
    }

}
