#include "ModelRegistry.h"

#include <map>

#include <diffsinger/Infer/InferenceService.h>

#include <synthrt/Core/Support/Error.h>
#include <synthrt/Driver/InferenceDriver.h>
#include <synthrt/Driver/InferenceSession.h>

namespace ds::infer {

    class ModelRegistry::Impl {
    public:
        std::map<std::string, srt::core::NO<srt::driver::InferenceSession>> m_sessions;
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

        // Return cached session if already bound for this inference id.
        auto it = _impl->m_sessions.find(manifest.id);
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
        _impl->m_sessions[manifest.id] = session;
        return session;
    }

    srt::core::Expected<srt::core::NO<srt::driver::InferenceSession>>
    ModelRegistry::getBoundSession(const std::string &inferenceId) const {
        auto it = _impl->m_sessions.find(inferenceId);
        if (it == _impl->m_sessions.end()) {
            return srt::core::Error(srt::core::Error::FileNotFound,
                                    "ModelRegistry::getBoundSession: inference id not bound");
        }
        return it->second;
    }

} // namespace ds::infer
