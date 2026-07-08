#include <diffsinger/Bank/ValidationReport.h>

namespace ds::bank {

    void ValidationReport::add(Severity severity, const std::string &message,
                               const std::string &path) {
        if (severity == Severity::Error) {
            _hasErrors = true;
        } else if (severity == Severity::Warning) {
            _hasWarnings = true;
        }
        _items.emplace_back(severity, message, path);
    }

    void ValidationReport::add(Severity severity, const std::string &message,
                               const std::string &path, const std::string &actualValue,
                               const std::string &recommendation) {
        if (severity == Severity::Error) {
            _hasErrors = true;
        } else if (severity == Severity::Warning) {
            _hasWarnings = true;
        }
        _items.emplace_back(severity, message, path, actualValue, recommendation);
    }

    bool ValidationReport::hasErrors() const {
        return _hasErrors;
    }

    bool ValidationReport::hasWarnings() const {
        return _hasWarnings;
    }

    const std::vector<ValidationItem> &ValidationReport::items() const {
        return _items;
    }

}
