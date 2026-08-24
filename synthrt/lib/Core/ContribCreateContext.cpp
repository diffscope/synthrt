#include "ContribCreateContext.h"
#include "ContribCreateContext_p.h"

#include <cassert>

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

    const std::string &ContribCreateContext::interface() const {
        assert(m_data->manifestDeclaration);
        return m_data->interface;
    }

    const std::string &ContribCreateContext::variant() const {
        assert(m_data->manifestDeclaration);
        return m_data->variant;
    }

    int ContribCreateContext::level() const {
        assert(m_data->manifestDeclaration);
        return m_data->level;
    }

    const JsonValue &ContribCreateContext::manifestExports() const {
        assert(m_data->manifestDeclaration);
        return m_data->manifestExports;
    }

    const JsonValue &ContribCreateContext::manifestConfiguration() const {
        assert(m_data->manifestDeclaration);
        return m_data->manifestConfiguration;
    }

    stdc::array_view<ContribSpec::Import> ContribCreateContext::imports() const {
        assert(m_data->manifestDeclaration);
        return m_data->imports;
    }

    ContribCreateContext::ContribCreateContext(const Data &data) : m_data(&data) {
    }

}
