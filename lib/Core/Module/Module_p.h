#pragma once

#include <list>
#include <map>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <stdcorelib/support/versionnumber.h>

#include <synthrt/Core/Core/NamedObject.h>
#include <synthrt/Core/Module/Module.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Core/Support/JSON.h>
#include <synthrt/Core/Task/ITask.h>

#include "../Core/NamedObject_p.h"

namespace srt::core {

    // Impl 为独立式（非继承 NamedObject::Impl）：ModuleSpec 不继承 NamedObject，
    // 故 Impl 无 _decl 反向指针，与继承式 Impl（如 PackageManager::Impl）风格不同。
    //
    // 与 LangCore 版本的差异（部分迁移）：
    //   - DisplayText 未迁移：name / configurationDisplayNames 改用 std::string
    //   - Package 未迁移：移除 PackageData *package，改用 packageId + packageVersion
    //   - ContextKey 已迁入 srt::core::ContextKey（见 Support/ContextKey.h）
    class ModuleSpec::Impl {
    public:
        explicit Impl(std::string category) : category(std::move(category)), state(Invalid) {}
        virtual ~Impl() = default;

        // v2 Phase 5: inlined so derived Impl classes in separate DLLs (srt-svs)
        // don't need to link the out-of-line definition from srt-core.
        virtual Expected<void> read(const std::filesystem::path &basePath, const JsonObject &obj) {
            // Minimal manifest reader — extracts id, className, apiLevel, configuration.
            // Full manifest parsing requires PackageManager (not yet migrated).
            auto it = obj.find("id");
            if (it != obj.end() && it->second.isString()) {
                id = it->second.toString();
            }
            it = obj.find("className");
            if (it != obj.end() && it->second.isString()) {
                className = it->second.toString();
            }
            it = obj.find("apiLevel");
            if (it != obj.end() && it->second.isInt()) {
                apiLevel = static_cast<int>(it->second.toInt());
            }
            it = obj.find("configuration");
            if (it != obj.end() && it->second.isObject()) {
                manifestConfiguration = it->second.toObject();
            }
            path = basePath;
            return {};
        }

        std::string id;

        std::string category;

        std::filesystem::path path;

        std::string className;

        // TODO: DisplayText not yet migrated; using std::string as placeholder.
        std::string name;
        int apiLevel = 0;

        JsonObject manifestConfiguration;
        NO<TaskConfiguration> configuration;

        // TODO: DisplayText not yet migrated; using std::string values.
        std::map<std::string, std::string> configurationDisplayNames;

        stdc::VersionNumber fmtVersion;

        State state;

        // PackageData 未迁移，使用 packageId + packageVersion 标识所属包。
        std::string packageId;
        stdc::VersionNumber packageVersion;

        // 模块所属 context（createModuleTask 阶段注入；parseSpec/loadSpec 阶段为默认值）
        ContextKey contextKey;

        Runtime *runtime = nullptr;
    };

    // ModuleCategory::Impl 继承 ObjectPool::Impl（ModuleCategory 继承 ObjectPool）。
    //
    // 与 LangCore 版本的差异：
    //   - PackageManager 在 srt::g2p 中定义：mgr 改为 void* 以避免 srt-core 对 srt-g2p
    //     的循环依赖。srt::g2p::PackageManager 传入 this，并 static_cast 回来使用。
    //   - su_mtx() 暂为本地 shared_mutex（粒度从 per-manager 变为 per-category，
    //     属临时方案；后续可考虑通过 void* mgr 共享 PackageManager 的锁）。
    //   - findModuleSpecs / loadSpecBase 等依赖 PackageManager 的方法在 .cpp 中 stub。
    class ModuleCategory::Impl : public ObjectPool::Impl {
    public:
        explicit Impl(ModuleCategory *decl, std::string name, void *mgr) :
            ObjectPool::Impl(decl), name(std::move(name)), mgr(mgr) {}
        explicit Impl(ModuleCategory *decl, std::string name, Runtime *runtime) :
            ObjectPool::Impl(decl), name(std::move(name)), mgr(runtime), runtime(runtime) {}
        ~Impl() override;

        std::string name;
        void *mgr;
        Runtime *runtime = nullptr;

        std::list<ModuleSpec *> modules;
        std::map<
            std::string,
            std::unordered_map<stdc::VersionNumber, std::map<std::string, std::map<int, decltype(modules)::iterator>>>>
            indexes;

        // TODO: 临时本地锁；PackageManager 迁移后应改回 mgr->_impl->su_mtx。
        mutable std::shared_mutex su_mtx;
        std::shared_mutex &suMtx() const { return su_mtx; }

        std::vector<ModuleSpec *> findModuleSpecs(const ModuleLocator &loc) const;
    };

} // namespace srt::core
