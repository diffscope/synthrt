#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <stdcorelib/support/versionnumber.h>

#include <synthrt/Core/srt_core_global.h>
#include <synthrt/Core/Core/Runtime.h>
#include <synthrt/Core/Core/NamedObject.h>
#include <synthrt/Core/Support/ContextKey.h>
#include <synthrt/Core/Support/DisplayText.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Core/Support/JSON.h>
#include <synthrt/Core/Task/ITask.h>

namespace srt::core {

    class Runtime;

    // Package and PackageManager are defined in srt::g2p (G2P-specific).
    // ModuleCategory stores a void* manager pointer to avoid a circular
    // dependency; srt::g2p::PackageManager passes `this` and casts back.

    // Forward declaration of the registrar template used by the
    // SRT_CORE_DEFINE_MODULE_CATEGORY macro. The full definition lives below
    // and registers categories with srt::core::Runtime.
    template <class T>
    class ModuleCategoryRegistrar;

} // namespace srt::core

// Forward-declare srt::g2p types so friend declarations in ModuleSpec /
// ModuleCategory resolve. srt-core does not depend on srt-g2p at link time;
// these declarations only grant friendship (one-way).
namespace srt::g2p {
    class PackageManager;
    class Package;
    class PackageData;
}

namespace srt::core {

    /// ModuleLocator - composite locator for a module spec.
    ///
    /// A locator uniquely identifies a ModuleSpec by (package, version, id).
    /// Any subset of the three components may be omitted, in which case the
    /// locator matches all specs that satisfy the supplied components.
    class SRT_CORE_EXPORT ModuleLocator {
    public:
        ModuleLocator(std::string package, const stdc::VersionNumber version, std::string id) :
            _package(std::move(package)), _version(version), _id(std::move(id)) {}
        ModuleLocator(std::string package, const stdc::VersionNumber version) :
            _package(std::move(package)), _version(version) {}
        ModuleLocator(std::string package, std::string id) : _package(std::move(package)), _id(std::move(id)) {}
        explicit ModuleLocator(std::string id) : _id(std::move(id)) {}

        ModuleLocator() = default;

        const std::string &package() const { return _package; }
        stdc::VersionNumber version() const { return _version; }
        const std::string &id() const { return _id; }
        bool isEmpty() const { return _package.empty() && _version.isEmpty() && _id.empty(); }

        std::string toString() const;
        static ModuleLocator fromString(const std::string_view &token);
        static bool isValidLocator(const std::string_view &token);

        bool operator==(const ModuleLocator &other) const {
            return _package == other._package && _version == other._version && _id == other._id;
        }

        bool operator!=(const ModuleLocator &other) const { return !(*this == other); }

    private:
        std::string _package;
        stdc::VersionNumber _version;
        std::string _id;
    };

    class PackageData;

    /// ModuleSpec - describes a single loaded module instance.
    ///
    /// Migrated from LangCore::ModuleSpec. Methods that depend on types not
    /// yet migrated (Package, PackageManager) are stubbed with TODO comments;
    /// see the .cpp for details. Localized texts use DisplayText per
    /// ds-spec 2.4 (BCP 47 tags, RFC 4647 Lookup on resolution).
    class SRT_CORE_EXPORT ModuleSpec {
    public:
        enum State {
            Invalid,
            Initialized,
            Ready,
            Finished,
            Deleted,
        };

        virtual ~ModuleSpec();

        const std::string &id() const;
        const std::string &category() const;
        const std::string &className() const;

        /// 模块名称（多语言文本，全部翻译随对象携带）。
        /// 用 text(locale) 按 BCP 47 偏好取词，如 name().text("zh-Hans-CN")。
        DisplayText name() const;

        int apiLevel() const;

        const JsonObject &manifestConfiguration() const;
        NO<TaskConfiguration> configuration() const;
        const std::filesystem::path &path() const;

        const std::string &packageId() const;
        const stdc::VersionNumber &packageVersion() const;

        /// 配置键的显示名称（多语言文本）；无翻译时退化为配置键本身。
        DisplayText configurationDisplayName(const std::string &configKey) const;

        State state() const;
        Runtime *runtime() const;

        // TODO: Package parent() const; — Package not yet migrated. Returns
        // by value, so a forward declaration is insufficient. Re-enable when
        // Package is migrated to srt::core:: (or srt::g2p::).
        // Package parent() const;

        // TODO: PackageManager *Mgr() const; — PackageManager not yet
        // migrated. Re-enable when PackageManager is available.
        // PackageManager *Mgr() const;

        /// 返回此模块所属的 ContextKey。
        /// 默认 context 模块返回 ContextKey()（空 context + 空 version）。
        /// 声库私有模块返回 ContextKey(singerId, packageVersion)。
        /// 注：contextKey 在 createModuleTask 阶段注入，parseSpec/loadSpec 阶段为默认值。
        // TODO: ContextKey is currently aliased to std::string (see top of file).
        ContextKey contextKey() const;

        template <class T>
        constexpr T *as();

        template <class T>
        constexpr const T *as() const;

    protected:
        class Impl;
        std::unique_ptr<Impl> _impl;
        explicit ModuleSpec(Impl &impl);
        explicit ModuleSpec(std::string category);

        friend class ModuleCategory;
        friend class ::srt::g2p::PackageManager;
        friend class ::srt::g2p::Package;
        friend class ::srt::g2p::PackageData;
        friend class Runtime;
    };

    template <class T>
    constexpr T *ModuleSpec::as() {
        static_assert(std::is_base_of_v<ModuleSpec, T>, "T must inherit from srt::core::ModuleSpec");
        return static_cast<T *>(this);
    }

    template <class T>
    constexpr const T *ModuleSpec::as() const {
        static_assert(std::is_base_of_v<ModuleSpec, T>, "T must inherit from srt::core::ModuleSpec");
        return static_cast<const T *>(this);
    }

    /// ModuleCategory - registry of ModuleSpec instances belonging to one
    /// category (e.g. "inference", "g2p"). Extends ObjectPool.
    ///
    /// Migrated from LangCore::ModuleCategory. Methods that depend on
    /// PackageManager (findSpec/specs/parseSpec/loadSpec) are kept but
    /// stubbed in the .cpp since PackageManager is not yet migrated.
    class SRT_CORE_EXPORT ModuleCategory : public ObjectPool {
    public:
        ~ModuleCategory() override;

        const std::string &name() const;
        Runtime *runtime() const;

        // TODO: PackageManager *Mgr() const; — PackageManager not yet migrated.
        // PackageManager *Mgr() const;

        std::vector<ModuleSpec *> findSpec(const ModuleLocator &identifier) const;
        std::vector<ModuleSpec *> specs() const;

        template <class T>
        constexpr T *as();

        template <class T>
        constexpr const T *as() const;

    protected:
        virtual std::string key() const = 0;
        virtual std::string category() const = 0;

        virtual Expected<ModuleSpec *> parseSpec(const std::filesystem::path &basePath,
                                                 const JsonValue &config) const;
        Expected<void> loadSpecBase(ModuleSpec *spec, ModuleSpec::State state);
        virtual Expected<void> loadSpec(ModuleSpec *spec, ModuleSpec::State state);

        std::vector<ModuleSpec *> find(const ModuleLocator &loc) const;

        class Impl;
        explicit ModuleCategory(Impl &impl);
        /// Construct with a manager pointer. The manager type is opaque to
        /// srt-core (defined as void* to avoid a circular dependency on
        /// srt::g2p::PackageManager). Downstream code passes `this` and
        /// casts back via static_cast<srt::g2p::PackageManager*>(mgr()).
        ModuleCategory(std::string name, void *mgr);
        ModuleCategory(std::string name, Runtime *runtime);

        /// Access the opaque manager pointer.
        void *mgr() const;

        friend class ::srt::g2p::PackageManager;
        friend class ::srt::g2p::Package;
        friend class ::srt::g2p::PackageData;
        friend class Runtime;
    };

    template <class T>
    constexpr T *ModuleCategory::as() {
        static_assert(std::is_base_of_v<ModuleCategory, T>, "T must inherit from srt::core::ModuleCategory");
        return static_cast<T *>(this);
    }

    template <class T>
    constexpr const T *ModuleCategory::as() const {
        static_assert(std::is_base_of_v<ModuleCategory, T>, "T must inherit from srt::core::ModuleCategory");
        return static_cast<const T *>(this);
    }

    template <class T>
    class ModuleCategoryRegistrar {
        static_assert(std::is_base_of_v<ModuleCategory, T>,
                      "T must inherit from srt::core::ModuleCategory");

    public:
        ModuleCategoryRegistrar() {
            Runtime::registerModuleCategoryFactory([](Runtime *runtime) -> ModuleCategory * {
                return new T(runtime);
            });
        }
    };

} // namespace srt::core

// ============================================================================
// 简化的模块类别注册宏
// ============================================================================

/// SRT_CORE_DECLARE_MODULE_CATEGORY - 在头文件中声明模块类别
/// 使用示例（在 .h 文件中）：
///   namespace srt::core {
///       class MyTask;
///       SRT_CORE_DECLARE_MODULE_CATEGORY(My, "my-category")
///   }
#define SRT_CORE_DECLARE_MODULE_CATEGORY(ClassName, CategoryKey) \
    class ClassName##Spec : public ::srt::core::ModuleSpec { \
    public: \
        ~ClassName##Spec() override; \
    protected: \
        class Impl; \
        ClassName##Spec(); \
        friend class ClassName##Category; \
    }; \
    class ClassName##Category : public ::srt::core::ModuleCategory { \
    public: \
        ~ClassName##Category() override; \
    protected: \
        std::string key() const override; \
        std::string category() const override; \
        class Impl; \
        explicit ClassName##Category(::srt::core::Runtime *runtime); \
        friend class ::srt::core::ModuleCategoryRegistrar<ClassName##Category>; \
    };

/// SRT_CORE_DEFINE_MODULE_CATEGORY - 在实现文件中定义模块类别
/// 使用示例（在 .cpp 文件中）：
///   SRT_CORE_DEFINE_MODULE_CATEGORY(My, "my-category")
#define SRT_CORE_DEFINE_MODULE_CATEGORY(ClassName, CategoryKey) \
    namespace srt::core { \
        class ClassName##Spec::Impl : public ModuleSpec::Impl { \
        public: \
            Impl(const std::string &category) : ModuleSpec::Impl(category) {} \
        }; \
        ClassName##Spec::ClassName##Spec() : ModuleSpec(*new Impl(CategoryKey)) {} \
        ClassName##Spec::~ClassName##Spec() = default; \
        class ClassName##Category::Impl : public ModuleCategory::Impl { \
        public: \
            Impl(ClassName##Category *decl, const std::string &category, ::srt::core::Runtime *runtime) : \
                ModuleCategory::Impl(decl, category, runtime) {} \
            ~Impl() override = default; \
        }; \
        ClassName##Category::ClassName##Category(::srt::core::Runtime *runtime) : \
            ModuleCategory(CategoryKey, runtime) {} \
        ClassName##Category::~ClassName##Category() = default; \
        std::string ClassName##Category::key() const { return CategoryKey; } \
        std::string ClassName##Category::category() const { return CategoryKey; } \
        static ModuleCategoryRegistrar<ClassName##Category> g_##ClassName##Registrar; \
    }

// 注意：使用 SRT_CORE_DEFINE_MODULE_CATEGORY 宏的 .cpp 文件需要包含：
// #include "Module/Module_p.h"
// #include "PackageManager_p.h"  (when PackageManager is migrated)

/// SRT_CORE_REGISTER_MODULE_CATEGORY - 便捷宏，同时在头文件和实现文件中使用
/// 使用示例（在 .h 文件中）：
///   SRT_CORE_REGISTER_MODULE_CATEGORY_DECLARE(My, "my-category")
/// 使用示例（在 .cpp 文件中）：
///   SRT_CORE_REGISTER_MODULE_CATEGORY_DEFINE(My, "my-category")
#define SRT_CORE_REGISTER_MODULE_CATEGORY_DECLARE(ClassName, CategoryKey) \
    SRT_CORE_DECLARE_MODULE_CATEGORY(ClassName, CategoryKey)

#define SRT_CORE_REGISTER_MODULE_CATEGORY_DEFINE(ClassName, CategoryKey) \
    SRT_CORE_DEFINE_MODULE_CATEGORY(ClassName, CategoryKey)
