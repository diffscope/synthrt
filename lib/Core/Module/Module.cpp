#include <synthrt/Core/Module/Module.h>
#include "Module_p.h"

#include <mutex>
#include <shared_mutex>
#include <utility>

#include <stdcorelib/pimpl.h>

namespace srt::core {

    // ============================================================================
    // ModuleLocator
    // ============================================================================

    std::string ModuleLocator::toString() const {
        if (_package.empty()) {
            return _id;
        }
        if (_version.isEmpty()) {
            if (_id.empty()) {
                return _package;
            }
            return _package + "/" + _id;
        }
        if (_id.empty()) {
            return _package + "[" + _version.toString() + "]";
        }
        return _package + "[" + _version.toString() + "]/" + _id;
    }

    ModuleLocator ModuleLocator::fromString(const std::string_view &token) {
        if (token.empty()) {
            return {};
        }

        ModuleLocator result;
        const auto slash = token.find('/');
        if (slash == std::string::npos) {
            if (!isValidLocator(token)) {
                return {};
            }
            result._id = token;
            return result;
        }

        const auto leftPart = token.substr(0, slash);
        const auto rightPart = token.substr(slash + 1);
        if (!isValidLocator(rightPart)) {
            return {};
        }
        result._id = rightPart;

        const auto openBracket = leftPart.find('[');
        if (openBracket == std::string::npos) {
            if (!isValidLocator(leftPart)) {
                return {};
            }
            result._package = leftPart;
            return result;
        }

        if (leftPart.back() != ']') {
            return {};
        }
        const auto package = leftPart.substr(0, openBracket);
        if (!isValidLocator(package)) {
            return {};
        }
        result._package = package;
        result._version = stdc::VersionNumber::fromString(
            leftPart.substr(openBracket + 1, leftPart.size() - openBracket - 2)).value_or(stdc::VersionNumber());
        return result;
    }

    bool ModuleLocator::isValidLocator(const std::string_view &token) {
        if (token.empty()) {
            return false;
        }
        for (const auto &ch : token) {
            switch (ch) {
                case '/':
                case '\\':
                case '[':
                case ']':
                case ':':
                case ';':
                case '\'':
                case '"':
                    return false;
                default:
                    break;
            }
        }
        return true;
    }

    // ============================================================================
    // ModuleSpec
    // ============================================================================

    ModuleSpec::~ModuleSpec() = default;

    const std::string &ModuleSpec::id() const {
        return _impl->m_id;
    }

    const std::string &ModuleSpec::category() const {
        return _impl->m_category;
    }

    const std::string &ModuleSpec::className() const {
        return _impl->m_className;
    }

    DisplayText ModuleSpec::name() const {
        return _impl->m_name;
    }

    int ModuleSpec::apiLevel() const {
        return _impl->m_apiLevel;
    }

    const JsonObject &ModuleSpec::manifestConfiguration() const {
        return _impl->m_manifestConfiguration;
    }

    NO<TaskConfiguration> ModuleSpec::configuration() const {
        return _impl->m_configuration;
    }

    const std::filesystem::path &ModuleSpec::path() const {
        return _impl->m_path;
    }

    const std::string &ModuleSpec::packageId() const {
        return _impl->m_packageId;
    }

    const stdc::VersionNumber &ModuleSpec::packageVersion() const {
        return _impl->m_packageVersion;
    }

    DisplayText ModuleSpec::configurationDisplayName(const std::string &configKey) const {
        auto it = _impl->m_configurationDisplayNames.find(configKey);
        if (it != _impl->m_configurationDisplayNames.end()) {
            return it->second;
        }
        return DisplayText(configKey);
    }

    ModuleSpec::State ModuleSpec::state() const {
        return _impl->m_state;
    }

    Runtime *ModuleSpec::runtime() const {
        return _impl->m_runtime;
    }

    // TODO: Package not yet migrated.
    // Package ModuleSpec::parent() const { ... }

    // TODO: PackageManager not yet migrated.
    // PackageManager *ModuleSpec::Mgr() const { ... }

    ContextKey ModuleSpec::contextKey() const {
        return _impl->m_contextKey;
    }

    ModuleSpec::ModuleSpec(Impl &impl) : _impl(&impl) {
    }

    ModuleSpec::ModuleSpec(std::string category) :
        _impl(new Impl(std::move(category))) {
    }

    // ============================================================================
    // ModuleSpec::Impl
    // ============================================================================
    // v2 Phase 5: read() moved inline to Module_p.h so derived Impl classes in
    // separate DLLs (srt-svs) don't need to link the out-of-line definition.

    // ============================================================================
    // ModuleCategory
    // ============================================================================

    ModuleCategory::~ModuleCategory() = default;

    const std::string &ModuleCategory::name() const {
        auto &impl = *static_cast<Impl *>(_impl.get());
        return impl.m_name;
    }

    void *ModuleCategory::mgr() const {
        auto &impl = *static_cast<Impl *>(_impl.get());
        return impl.m_mgr;
    }

    Runtime *ModuleCategory::runtime() const {
        auto &impl = *static_cast<Impl *>(_impl.get());
        return impl.m_runtime;
    }

    std::vector<ModuleSpec *> ModuleCategory::findSpec(const ModuleLocator &identifier) const {
        auto &impl = *static_cast<Impl *>(_impl.get());
        std::shared_lock<std::shared_mutex> lock(impl.suMtx());
        return impl.findModuleSpecs(identifier);
    }

    std::vector<ModuleSpec *> ModuleCategory::specs() const {
        auto &impl = *static_cast<Impl *>(_impl.get());
        std::shared_lock<std::shared_mutex> lock(impl.suMtx());
        std::vector<ModuleSpec *> res;
        res.reserve(impl.m_modules.size());
        for (const auto &item : impl.m_modules) {
            res.push_back(static_cast<ModuleSpec *>(item));
        }
        return res;
    }

    Expected<ModuleSpec *> ModuleCategory::parseSpec(const std::filesystem::path &basePath,
                                                     const JsonValue &config) const {
        (void) basePath;
        (void) config;
        return Error(Error::NotImplemented, "ModuleCategory::parseSpec is not implemented");
    }

    Expected<void> ModuleCategory::loadSpecBase(ModuleSpec *spec, ModuleSpec::State state) {
        if (!spec) {
            return Error(Error::InvalidArgument, "ModuleCategory::loadSpecBase received null spec");
        }

        auto &impl = *static_cast<Impl *>(_impl.get());
        auto &specImpl = *spec->_impl;
        switch (state) {
            case ModuleSpec::Initialized: {
                std::unique_lock<std::shared_mutex> lock(impl.suMtx());
                specImpl.m_state = state;
                specImpl.m_runtime = impl.m_runtime;
                impl.m_modules.push_back(spec);
                return {};
            }

            case ModuleSpec::Ready:
            case ModuleSpec::Finished: {
                std::unique_lock<std::shared_mutex> lock(impl.suMtx());
                specImpl.m_state = state;
                return {};
            }

            case ModuleSpec::Deleted: {
                std::unique_lock<std::shared_mutex> lock(impl.suMtx());
                impl.m_modules.remove(spec);
                specImpl.m_state = state;
                return {};
            }

            default:
                return Error(Error::InvalidArgument, "invalid ModuleSpec state");
        }
    }

    Expected<void> ModuleCategory::loadSpec(ModuleSpec *spec, ModuleSpec::State state) {
        return loadSpecBase(spec, state);
    }

    std::vector<ModuleSpec *> ModuleCategory::find(const ModuleLocator &loc) const {
        auto &impl = *static_cast<Impl *>(_impl.get());
        return impl.findModuleSpecs(loc);
    }

    ModuleCategory::ModuleCategory(Impl &impl) : ObjectPool(impl) {
    }

    ModuleCategory::ModuleCategory(std::string name, void *mgr) :
        ObjectPool(*new Impl(this, std::move(name), mgr)) {
    }

    ModuleCategory::ModuleCategory(std::string name, Runtime *runtime) :
        ObjectPool(*new Impl(this, std::move(name), runtime)) {
    }

    // ============================================================================
    // ModuleCategory::Impl
    // ============================================================================

    ModuleCategory::Impl::~Impl() {
        // modules owns its ModuleSpec* entries (push_back in loadSpec takes
        // ownership). The Runtime rollback path removes specs from the list
        // via loadSpec(Deleted) before deleting them, so they are no longer
        // here — safe to delete whatever remains.
        for (auto *spec : m_modules) {
            delete spec;
        }
    }

    std::vector<ModuleSpec *> ModuleCategory::Impl::findModuleSpecs(const ModuleLocator &loc) const {
        // Minimal implementation: linear scan honoring the complete locator.
        // Full implementation uses the indexes map (requires PackageManager).
        std::vector<ModuleSpec *> result;
        for (auto *spec : m_modules) {
            if (!loc.id().empty() && spec->id() != loc.id()) {
                continue;
            }
            if (!loc.package().empty() && spec->packageId() != loc.package()) {
                continue;
            }
            if (!loc.version().isEmpty() && spec->packageVersion() != loc.version()) {
                continue;
            }
            result.push_back(spec);
        }
        return result;
    }

} // namespace srt::core
