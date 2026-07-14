#include "ModelRegistry.h"

#include <map>
#include <utility>

#include <diffsinger/Infer/InferenceService.h>

#include <synthrt/Core/Support/Error.h>
#include <synthrt/Driver/InferenceDriver.h>
#include <synthrt/Driver/InferenceSession.h>

namespace ds::infer {

    // Composite cache key: (packageId, inferenceId). Using std::pair keeps the
    // lookup logic simple without inventing a custom delimiter that could
    // collide with legitimate id strings.
    using CacheKey = std::pair<std::string, std::string>;

    class ModelRegistry::Impl {
    public:
        std::map<CacheKey, srt::core::NO<srt::driver::InferenceSession>> m_sessions;
    };

    ModelRegistry::ModelRegistry() : _impl(std::make_unique<Impl>()) {
    }

    ModelRegistry::~ModelRegistry() = default;

    srt::core::Expected<srt::core::NO<srt::driver::InferenceSession>>
    ModelRegistry::bind(const ds::bank::InferenceInfo &manifest,
                        srt::driver::InferenceDriver *driver) {
        if (!driver) {
            return srt::core::Error(srt::core::Error::InvalidArgument,
                                    "ModelRegistry::bind: driver is null");
        }

        // Cache key is (packageId, inferenceId) so that two packages that both
        // define an inference with id "pitch" keep independent sessions
        // (ARCH-06). Without the packageId component, the second package's
        // bind() would incorrectly return the first package's cached session.
        const CacheKey key{manifest.packageId, manifest.id};
        auto it = _impl->m_sessions.find(key);
        if (it != _impl->m_sessions.end()) {
            return it->second;
        }

        // Create a new session through the driver.
        auto session = driver->createSession();
        if (!session) {
            return srt::core::Error(srt::core::Error::SessionError,
                                    "ModelRegistry::bind: failed to create session");
        }

        // Open every model path declared by the manifest.
        for (const auto &modelPath : manifest.modelPaths) {
            auto res = session->open(
                modelPath.second, srt::core::NO<srt::driver::InferenceSessionOpenArgs>());
            if (!res) {
                return res.takeError();
            }
        }

        // Cache and return the loaded session.
        _impl->m_sessions[key] = session;
        return session;
    }

    srt::core::Expected<srt::core::NO<srt::driver::InferenceSession>>
    ModelRegistry::getBoundSession(const std::string &packageId,
                                   const std::string &inferenceId) const {
        auto it = _impl->m_sessions.find({packageId, inferenceId});
        if (it == _impl->m_sessions.end()) {
            return srt::core::Error(srt::core::Error::FileNotFound,
                                    "ModelRegistry::getBoundSession: "
                                    "inference id not bound for package " +
                                        packageId);
        }
        return it->second;
    }

} // namespace ds::infer
