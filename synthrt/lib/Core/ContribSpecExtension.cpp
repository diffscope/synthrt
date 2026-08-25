#include "ContribSpecExtension.h"

#include <utility>

STDC_INSTANTIATE_STATIC_REGISTRY_EXPORT(srt::ContribSpecExtensionFactory, SYNTHRT_EXPORT)

namespace srt {

    ContribSpecExtension::ContribSpecExtension(ContribSpec &spec, std::string id)
        : m_spec(&spec), m_id(std::move(id)) {
    }

    ContribSpecExtension::~ContribSpecExtension() = default;

    const std::string &ContribSpecExtension::id() const {
        return m_id;
    }

    ContribSpec &ContribSpecExtension::spec() const {
        return *m_spec;
    }

}
