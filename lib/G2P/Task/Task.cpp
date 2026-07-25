#include <synthrt/G2P/Task/Task.h>
#include "Task_p.h"

#include <filesystem>
#include <fstream>
#include <mutex>

#include <stdcorelib/path.h>
#include <stdcorelib/str.h>

#include <synthrt/Core/Module/Module.h>
#include <synthrt/G2P/Core/PackageManager.h>

namespace srt::g2p {

    Task::Task() : d(std::make_unique<Impl>()) {}

    Task::Task(const srt::core::ModuleSpec *spec) : d(std::make_unique<Impl>()) {
        d->m_spec = spec;
        if (spec) {
            d->m_cachedApiLevel = spec->apiLevel();
        }
    }

    Task::~Task() = default;

    const ModuleSpec *Task::spec() const {
        return d->m_spec;
    }

    PackageManager *Task::Mgr() const {
        return d->m_mgr;
    }

    void Task::setMgr(PackageManager *mgr) {
        d->m_mgr = mgr;
    }

    srt::core::Expected<srt::core::NO<srt::core::NamedObject>> Task::getObject(
        const std::string &category, const std::string &id) const {
        const auto mgr = Mgr();
        if (!mgr)
            return Error(Error::RuntimeError, "manager is not available");
        const auto inferenceCate = mgr->category(category);
        if (!inferenceCate)
            return Error(Error::RuntimeError, "could not find category: " + category);

        const auto inferenceObject = inferenceCate->getFirstObject(id);
        if (!inferenceObject)
            return Error(Error::RuntimeError, "could not find id: " + id);

        return inferenceObject;
    }

    std::string Task::getConfig() const {
        std::shared_lock lock(d->m_mutex);
        return d->m_config;
    }

    srt::core::Expected<std::string> Task::loadConfig() const {
        // 从默认配置路径加载配置
        if (!d->m_spec)
            return Error(Error::RuntimeError, "spec is not available");

        auto packagePath = d->m_spec->path();
        auto manifestConfig = d->m_spec->manifestConfiguration();

        // 解析配置路径（通常是相对路径）
        std::string configPathStr;
        auto configIt = manifestConfig.find("configuration");
        if (configIt != manifestConfig.end() && configIt->second.isString()) {
            configPathStr = configIt->second.toString();
        } else {
            configPathStr = "modules/" + d->m_spec->id() + "/config.json";
        }

        // std::filesystem 与 std::ifstream 属第三方库边界，异常需转为 Error（ROBUST-02）
        try {
            auto configPath = packagePath / configPathStr;

            if (!std::filesystem::exists(configPath)) {
                return Error(Error::FileSystemError,
                             "Config file not found: " + stdc::path::to_utf8(configPath));
            }

            std::ifstream file(configPath);
            if (!file.is_open()) {
                return Error(Error::FileSystemError,
                             "Failed to open config file: " + stdc::path::to_utf8(configPath));
            }

            std::string content((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());

            return content;
        } catch (const std::exception &e) {
            return Error(Error::FileSystemError,
                         std::string("Filesystem error while loading config: ") + e.what());
        }
    }

    srt::core::Expected<void> Task::initializeConfig() {
        auto loadResult = loadConfig();
        if (!loadResult) {
            return loadResult.error();
        }

        std::unique_lock lock(d->m_mutex);
        d->m_config = loadResult.value();

        return {};
    }

} // namespace srt::g2p
