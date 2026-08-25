#include "ContribSpec.h"
#include "ContribSpec_p.h"

#include <cassert>
#include <utility>

#include "ContribCategory_p.h"
#include "PackageHandle_p.h"

namespace srt {

    ContribImport::ContribImport(ContribImport &&other) noexcept = default;

    ContribImport &ContribImport::operator=(ContribImport &&other) noexcept = default;

    ContribImport::~ContribImport() = default;

    const std::string &ContribImport::role() const {
        assert(m_data);
        return m_data->role;
    }

    const ContribLocator &ContribImport::locator() const {
        assert(m_data);
        return m_data->locator;
    }

    const JsonValue &ContribImport::manifestOptions() const {
        assert(m_data);
        return m_data->manifestOptions;
    }

    const ContribImportOptions *ContribImport::options() const {
        assert(m_data);
        return m_data->binding ? &m_data->binding->options() : m_data->options.get();
    }

    ContribImportBinding *ContribImport::binding() const {
        assert(m_data);
        return m_data->binding.get();
    }

    ContribExecFactory *ContribImport::execFactory() const {
        assert(m_data);
        return m_data->execFactory.get();
    }

    ContribImport::ContribImport(std::string role, ContribLocator locator, JsonValue options)
        : m_data(std::make_shared<ContribImportData>(std::move(role), std::move(locator),
                                                     std::move(options))) {
    }

    ContribImport::ContribImport(const ContribImport &other) = default;

    ContribSpec::~ContribSpec() = default;

    const ContribLocator &ContribSpec::locator() const {
        return _impl->locator;
    }

    PackageHandle ContribSpec::package() const {
        assert(_impl->package);
        return PackageHandle(_impl->package->shared_from_this());
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

    const JsonObject &ContribSpec::manifestDeclaration() const {
        assert(_impl->hasModuleDeclaration);
        return _impl->manifestDeclaration;
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

    ContribSpec::ContribSpec(const ContribCreateContext &context)
        : _impl(std::make_unique<Impl>()) {
        _impl->package = context.m_data->package;
        _impl->locator = context.m_data->locator;
        if (!context.m_data->manifestDeclaration) {
            return;
        }

        _impl->hasModuleDeclaration = true;
        _impl->manifestDeclaration = *context.m_data->manifestDeclaration;
        _impl->name = context.m_data->name;
        _impl->interface = context.m_data->interface;
        _impl->variant = context.m_data->variant;
        _impl->level = context.m_data->level;
        _impl->manifestExports = context.m_data->manifestExports;
        _impl->manifestConfiguration = context.m_data->manifestConfiguration;
        _impl->imports.reserve(context.m_data->imports.size());
        for (const auto &item : context.m_data->imports) {
            _impl->imports.push_back(ContribImport(item));
        }
    }

    ContribInterpreter *ContribSpec::interpreter() const {
        return _impl->interpreter;
    }
}
