#include "ModelRegistry.h"

#include <map>
#include <shared_mutex>
#include <utility>
#include <vector>

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
        mutable std::shared_mutex m_mutex;
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
        std::unique_lock<std::shared_mutex> lock(_impl->m_mutex);
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
                // BUG-08: 失败时关闭已创建的 session，避免资源泄漏。close 失败
                // 不掩盖原 open 错误（ROBUST-05：显式检查而非 (void) 丢弃）。
                if (auto closeRes = session->close(); !closeRes) {
                    // close 失败时 session 将随 NO 引用计数归零而析构，忽略。
                }
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
        std::shared_lock<std::shared_mutex> lock(_impl->m_mutex);
        auto it = _impl->m_sessions.find({packageId, inferenceId});
        if (it == _impl->m_sessions.end()) {
            return srt::core::Error(srt::core::Error::FileNotFound,
                                    "ModelRegistry::getBoundSession: "
                                    "inference id not bound for package " +
                                        packageId);
        }
        return it->second;
    }

    bool ModelRegistry::unbind(const std::string &packageId,
                               const std::string &inferenceId) {
        std::unique_lock<std::shared_mutex> lock(_impl->m_mutex);
        auto it = _impl->m_sessions.find({packageId, inferenceId});
        if (it == _impl->m_sessions.end()) {
            return false;
        }
        auto session = it->second;
        _impl->m_sessions.erase(it);
        lock.unlock(); // 在锁外 close，避免长时间持锁
        if (session) {
            // close 失败不掩盖 unbind 结果：session 已从缓存移除，将随引用计数
            // 归零而析构。显式检查以符合 ROBUST-05（不使用 (void) 丢弃 Expected）。
            if (auto closeRes = session->close(); !closeRes) {
                // 忽略 close 错误，session 即将被释放
            }
        }
        return true;
    }

    size_t ModelRegistry::unbindPackage(const std::string &packageId) {
        std::unique_lock<std::shared_mutex> lock(_impl->m_mutex);
        size_t removed = 0;
        std::vector<srt::core::NO<srt::driver::InferenceSession>> sessionsToClose;
        for (auto it = _impl->m_sessions.begin(); it != _impl->m_sessions.end();) {
            if (it->first.first == packageId) {
                sessionsToClose.push_back(it->second);
                it = _impl->m_sessions.erase(it);
                ++removed;
            } else {
                ++it;
            }
        }
        lock.unlock(); // 在锁外 close
        for (auto &session : sessionsToClose) {
            if (session) {
                // 同 unbind：显式检查而非 (void) 丢弃（ROBUST-05）
                if (auto closeRes = session->close(); !closeRes) {
                    // 忽略 close 错误，session 即将被释放
                }
            }
        }
        return removed;
    }

} // namespace ds::infer
