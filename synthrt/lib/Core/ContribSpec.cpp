#include "ContribSpec.h"
#include "ContribSpec_p.h"

#include <cassert>
#include <utility>

#include "ContribCategory_p.h"
#include "PackageHandle_p.h"

namespace srt {

    const std::string &ContribImport::role() const {
        return m_data->role;
    }

    const ContribLocator &ContribImport::locator() const {
        return m_data->locator;
    }

    const JsonValue &ContribImport::manifestOptions() const {
        return m_data->manifestOptions;
    }

    const ContribImportOptions *ContribImport::options() const {
        return m_data->binding ? &m_data->binding->options() : m_data->options.get();
    }

    ContribImportBinding *ContribImport::binding() const {
        return m_data->binding.get();
    }

    ContribExecutiveFactory *ContribImport::executiveFactory() const {
        return m_data->executiveFactory.get();
    }

    ContribImport::ContribImport(const Data &data) : m_data(&data) {
    }

    ContribSpec::~ContribSpec() = default;

    const ContribLocator &ContribSpec::locator() const {
        return _impl->locator;
    }

    PackageHandle ContribSpec::package() const {
        assert(_impl->package);
        return PackageHandle(_impl->package->shared_from_this());
    }

    const std::filesystem::path &ContribSpec::declarationPath() const {
        assert(_impl->hasModuleDeclaration);
        return _impl->declarationPath;
    }

    const JsonObject &ContribSpec::manifestDeclaration() const {
        assert(_impl->hasModuleDeclaration);
        return _impl->manifestDeclaration;
    }

    const DisplayText &ContribSpec::name() const {
        assert(_impl->hasModuleDeclaration);
        return _impl->name;
    }

    const std::string &ContribSpec::interface() const {
        assert(_impl->hasModuleDeclaration);
        return _impl->interface;
    }

    const std::string &ContribSpec::variant() const {
        assert(_impl->hasModuleDeclaration);
        return _impl->variant;
    }

    int ContribSpec::level() const {
        assert(_impl->hasModuleDeclaration);
        return _impl->level;
    }

    const JsonValue &ContribSpec::manifestExports() const {
        assert(_impl->hasModuleDeclaration);
        return _impl->manifestExports;
    }

    const ContribExports *ContribSpec::exports() const {
        assert(_impl->hasModuleDeclaration);
        return _impl->exports.get();
    }

    const JsonValue &ContribSpec::manifestConfiguration() const {
        assert(_impl->hasModuleDeclaration);
        return _impl->manifestConfiguration;
    }

    const ContribConfiguration *ContribSpec::configuration() const {
        assert(_impl->hasModuleDeclaration);
        return _impl->configuration.get();
    }

    stdc::array_view<ContribImport> ContribSpec::imports() const {
        assert(_impl->hasModuleDeclaration);
        return _impl->imports;
    }

    std::optional<ContribImport> ContribSpec::findImport(std::string_view role) const {
        assert(_impl->hasModuleDeclaration);
        const auto it = _impl->importData.find(role);
        if (it == _impl->importData.end()) {
            return std::nullopt;
        }
        return ContribImport(it->second);
    }

    stdc::array_view<ContribSpecExtension *> ContribSpec::extensions() const {
        return _impl->extensions;
    }

    ContribSpecExtension *ContribSpec::findExtension(std::string_view id) const {
        const auto it = _impl->extensionData.find(id);
        return it == _impl->extensionData.end() ? nullptr : it->second.get();
    }

    ContribSpec::ContribSpec(const ContribCreateContext &context)
        : _impl(std::make_unique<Impl>()) {
        _impl->package = context.m_data->package;
        _impl->locator = context.m_data->locator;
        if (!context.m_data->manifestDeclaration) {
            return;
        }

        _impl->hasModuleDeclaration = true;
        assert(context.m_data->declarationPath);
        _impl->declarationPath = *context.m_data->declarationPath;
        _impl->manifestDeclaration = *context.m_data->manifestDeclaration;
        _impl->name = context.m_data->name;
        _impl->interface = context.m_data->interface;
        _impl->variant = context.m_data->variant;
        _impl->level = context.m_data->level;
        _impl->manifestExports = context.m_data->manifestExports;
        _impl->manifestConfiguration = context.m_data->manifestConfiguration;
        _impl->imports.reserve(context.m_data->imports.size());
        for (const auto &item : context.m_data->imports) {
            auto role = item.role();
            auto key = role;
            const auto [it, inserted] = _impl->importData.try_emplace(
                std::move(key), std::move(role), item.locator(), item.manifestOptions());
            assert(inserted);
            _impl->imports.push_back(ContribImport(it->second));
        }
    }

    ContribInterpreter *ContribSpec::interpreter() const {
        return _impl->interpreter;
    }

}
