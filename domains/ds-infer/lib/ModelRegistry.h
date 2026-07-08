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
    class DSINFER_EXPORT ModelRegistry {
    public:
        ModelRegistry();
        ~ModelRegistry();

        /// Bind an InferenceInfo to a driver, producing a loaded
        /// InferenceSession. The manifest's modelPaths are resolved against the
        /// package root by ds::bank before reaching the registry. Repeated
        /// bind() calls for the same inference id return the cached session.
        srt::core::Expected<srt::core::NO<srt::driver::InferenceSession>> bind(
            const ds::bank::InferenceInfo &manifest,
            srt::driver::InferenceDriver *driver);

        /// Return the session previously bound for \p inferenceId, or
        /// \c Error::FileNotFound when no session has been bound.
        srt::core::Expected<srt::core::NO<srt::driver::InferenceSession>>
            getBoundSession(const std::string &inferenceId) const;

    private:
        class Impl;
        std::unique_ptr<Impl> _impl;
    };

} // namespace ds::infer
