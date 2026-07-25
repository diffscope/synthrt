#pragma once

#include <memory>
#include <string>

#include <synthrt/Core/Core/NamedObject.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Driver/InferenceSession.h>

#include <diffsinger/Bank/PackageManifest.h>
#include <diffsinger/Infer/dsinfer/dsinfer_global.h>

namespace srt::driver {
    class InferenceDriver;
}

namespace ds::infer {

    /// ModelRegistry - Maps package inference resources (ds::bank::InferenceInfo)
    /// to loaded model graphs (srt::driver::InferenceSession bound to a driver).
    ///
    /// \see 01-target-architecture.md section 3.2
    ///
    /// Sessions are cached by composite key (packageId, inferenceId) so that
    /// two packages defining inferences with the same id (e.g. "pitch") do not
    /// collide (ARCH-06 cross-package stage sharing). Repeated bind() calls for
    /// the same (packageId, inferenceId) return the cached session.
    class DSINFER_EXPORT ModelRegistry {
    public:
        ModelRegistry();
        ~ModelRegistry();

        /// Bind an InferenceInfo to a driver, producing a loaded
        /// InferenceSession. The manifest's modelPaths are resolved against the
        /// package root by ds::bank before reaching the registry. The cache key
        /// is (manifest.packageId, manifest.id). Repeated bind() calls for the
        /// same key return the cached session.
        srt::core::Expected<srt::core::NO<srt::driver::InferenceSession>> bind(
            const ds::bank::InferenceInfo &manifest,
            srt::driver::InferenceDriver *driver);

        /// Return the session previously bound for (packageId, inferenceId), or
        /// \c Error::FileNotFound when no session has been bound. The packageId
        /// is required to isolate same-id inferences from different packages.
        srt::core::Expected<srt::core::NO<srt::driver::InferenceSession>>
            getBoundSession(const std::string &packageId,
                            const std::string &inferenceId) const;

        /// Remove the cached session for (packageId, inferenceId) and close it.
        /// Returns true if a session was removed. Safe to call on unknown keys
        /// (returns false). Callers MUST ensure no inference is running on the
        /// session before calling unbind (ROBUST-04).
        bool unbind(const std::string &packageId, const std::string &inferenceId);

        /// Remove all cached sessions belonging to a package and close them.
        /// Used when a voicebank package is unloaded. Returns the number of
        /// sessions removed.
        size_t unbindPackage(const std::string &packageId);

    private:
        class Impl;
        std::unique_ptr<Impl> _impl;
    };

} // namespace ds::infer
