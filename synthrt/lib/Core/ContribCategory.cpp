#include "ContribCategory.h"
#include "ContribCategory_p.h"

#include <mutex>
#include <utility>

#include "ContribExecutive.h"
#include "SynthUnit_p.h"

STDC_STATIC_REGISTRY(srt::ContribCategory)

namespace srt {

    const ContribLocator &ContribCreateContext::locator() const {
        return m_data->locator;
    }

    const JsonObject &ContribCreateContext::manifestEntry() const {
        return m_data->manifestEntry;
    }

    const std::filesystem::path *ContribCreateContext::declarationPath() const {
        return m_data->declarationPath ? &*m_data->declarationPath : nullptr;
    }

    const JsonObject *ContribCreateContext::manifestDeclaration() const {
        return m_data->manifestDeclaration ? &*m_data->manifestDeclaration : nullptr;
    }

    const DisplayText &ContribCreateContext::name() const {
        assert(m_data->manifestDeclaration);
        return m_data->name;
    }

    const ContribContractTriplet &ContribCreateContext::triplet() const {
        assert(m_data->manifestDeclaration);
        return m_data->triplet;
    }

    const JsonValue &ContribCreateContext::manifestExports() const {
        assert(m_data->manifestDeclaration);
        return m_data->manifestExports;
    }

    const JsonValue &ContribCreateContext::manifestConfiguration() const {
        assert(m_data->manifestDeclaration);
        return m_data->manifestConfiguration;
    }

    stdc::array_view<ContribImport> ContribCreateContext::imports() const {
        assert(m_data->manifestDeclaration);
        return m_data->imports;
    }

    ContribCreateContext::ContribCreateContext(const Data &data) : m_data(&data) {
    }

    ContribCategory::~ContribCategory() = default;

    const std::string &ContribCategory::name() const {
        return _impl->name;
    }

    ContribCategory::DeclarationMode ContribCategory::declarationMode() const noexcept {
        return _impl->declarationMode;
    }

    const std::string &ContribCategory::interpreterIid() const {
        assert(_impl->declarationMode == ModuleDeclaration);
        return _impl->interpreterIid;
    }

    SynthUnit &ContribCategory::synthUnit() const {
        assert(_impl->synthUnit);
        return *_impl->synthUnit;
    }

    std::vector<ContribSpec *> ContribCategory::contributions() const {
        if (!_impl->synthUnit) {
            return {};
        }
        std::lock_guard<std::recursive_mutex> lock(_impl->synthUnit->_impl->loadMutex);
        return _impl->contributions;
    }

    Expected<std::unique_ptr<ContribExecutiveFactory>>
        ContribCategory::createExecutiveFactory(ContribImportBinding &binding) const {
        return std::unique_ptr<ContribExecutiveFactory>();
    }

    ContribCategory::ContribCategory(std::string name, DeclarationMode declarationMode,
                                     std::string interpreterIid)
        : _impl(
              std::make_unique<Impl>(std::move(name), declarationMode, std::move(interpreterIid))) {
    }

}
