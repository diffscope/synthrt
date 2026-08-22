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
    // 故 Impl 无 m_q 反向指针，与继承式 Impl（如 PackageManager::Impl）风格不同。
    //
    // 与 LangCore 版本的差异（部分迁移）：
    //   - name / configurationDisplayNames 使用 DisplayText（ds-spec 2.4：BCP 47 键 +
    //     RFC 4647 Lookup；解析处用 fromJsonValueTolerant 以兼容缺 "_" 的存量包）
    //   - Package 未迁移：移除 PackageData *package，改用 packageId + packageVersion
    //   - ContextKey 已迁入 srt::core::ContextKey（见 Support/ContextKey.h）
    class ModuleSpec::Impl {
    public:
        explicit Impl(std::string category) : m_category(std::move(category)), m_state(Invalid) {}
        virtual ~Impl() = default;

        // v2 Phase 5: inlined so derived Impl classes in separate DLLs (srt-svs)
        // don't need to link the out-of-line definition from srt-core.
        virtual Expected<void> read(const std::filesystem::path &basePath, const JsonObject &obj) {
            // Minimal manifest reader — extracts id, className, apiLevel, configuration.
            // Full manifest parsing requires PackageManager (not yet migrated).
            auto it = obj.find("id");
            if (it != obj.end() && it->second.isString()) {
                m_id = it->second.toString();
            }
            it = obj.find("className");
            if (it != obj.end() && it->second.isString()) {
                m_className = it->second.toString();
            }
            it = obj.find("apiLevel");
            if (it != obj.end() && it->second.isInt()) {
                m_apiLevel = static_cast<int>(it->second.toInt());
            }
            it = obj.find("configuration");
            if (it != obj.end() && it->second.isObject()) {
                m_manifestConfiguration = it->second.toObject();
            }
            m_path = basePath;
            return {};
        }

        std::string m_id;

        std::string m_category;

        std::filesystem::path m_path;

        std::string m_className;

        // ds-spec 2.4 多语言文本：BCP 47 键，调用方按 RFC 4647 Lookup 取词。
        DisplayText m_name;
        int m_apiLevel = 0;

        JsonObject m_manifestConfiguration;
        NO<TaskConfiguration> m_configuration;

        // 配置键 -> 多语言显示名称。
        std::map<std::string, DisplayText> m_configurationDisplayNames;

        stdc::VersionNumber m_fmtVersion;

        State m_state;

        // PackageData 未迁移，使用 packageId + packageVersion 标识所属包。
        std::string m_packageId;
        stdc::VersionNumber m_packageVersion;

        // 模块所属 context（createModuleTask 阶段注入；parseSpec/loadSpec 阶段为默认值）
        ContextKey m_contextKey;

        Runtime *m_runtime = nullptr;
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
            ObjectPool::Impl(decl), m_name(std::move(name)), m_mgr(mgr) {}
        explicit Impl(ModuleCategory *decl, std::string name, Runtime *runtime) :
            ObjectPool::Impl(decl), m_name(std::move(name)), m_mgr(runtime), m_runtime(runtime) {}
        ~Impl() override;

        std::string m_name;
        void *m_mgr;
        Runtime *m_runtime = nullptr;

        std::list<ModuleSpec *> m_modules;
        std::map<
            std::string,
            std::unordered_map<stdc::VersionNumber, std::map<std::string, std::map<int, decltype(m_modules)::iterator>>>>
            m_indexes;

        // TODO: 临时本地锁；PackageManager 迁移后应改回 mgr->_impl->su_mtx。
        mutable std::shared_mutex m_su_mtx;
        std::shared_mutex &suMtx() const { return m_su_mtx; }

        std::vector<ModuleSpec *> findModuleSpecs(const ModuleLocator &loc) const;
    };

} // namespace srt::core
