#include <diffsinger/Bank/ValidationReport.h>

namespace ds::bank {

    void ValidationReport::add(Severity severity, const std::string &message,
                               const std::string &path) {
        if (severity == Severity::Error) {
            m_hasErrors = true;
        } else if (severity == Severity::Warning) {
            m_hasWarnings = true;
        }
        m_items.emplace_back(severity, message, path);
    }

    void ValidationReport::add(Severity severity, const std::string &message,
                               const std::string &path, const std::string &actualValue,
                               const std::string &recommendation) {
        if (severity == Severity::Error) {
            m_hasErrors = true;
        } else if (severity == Severity::Warning) {
            m_hasWarnings = true;
        }
        m_items.emplace_back(severity, message, path, actualValue, recommendation);
    }

    bool ValidationReport::hasErrors() const {
        return m_hasErrors;
    }

    bool ValidationReport::hasWarnings() const {
        return m_hasWarnings;
    }

    const std::vector<ValidationItem> &ValidationReport::items() const {
        return m_items;
    }

}
