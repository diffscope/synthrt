#pragma once

#include <string>
#include <vector>

#include <diffsinger/Bank/dsbank_global.h>

namespace ds::bank {

    /// ValidationItem - A single diagnostic produced during package validation.
    struct ValidationItem {
        enum Severity {
            Error,
            Warning,
            Info,
        };

        Severity severity = Error;
        std::string message;
        std::string path;
        std::string actualValue;
        std::string recommendation;

        ValidationItem() = default;
        ValidationItem(Severity sev, std::string msg, std::string p)
            : severity(sev), message(std::move(msg)), path(std::move(p)) {
        }
        ValidationItem(Severity sev, std::string msg, std::string p, std::string actual,
                       std::string rec)
            : severity(sev), message(std::move(msg)), path(std::move(p)),
              actualValue(std::move(actual)), recommendation(std::move(rec)) {
        }
    };

    /// ValidationReport - Collects diagnostics (errors/warnings/info) produced
    /// while validating a DiffSinger package against a schema version.
    class DSBANK_EXPORT ValidationReport {
    public:
        using Severity = ValidationItem::Severity;

    public:
        ValidationReport() = default;

    public:
        /// Appends a diagnostic. \p path is an optional JSON Pointer-like path
        /// (e.g. \c "singers/0/singerId") locating the source of the issue.
        void add(Severity severity, const std::string &message,
                 const std::string &path = "");
        void add(Severity severity, const std::string &message, const std::string &path,
                 const std::string &actualValue, const std::string &recommendation);

        bool hasErrors() const;
        bool hasWarnings() const;

        const std::vector<ValidationItem> &items() const;

    protected:
        std::vector<ValidationItem> _items;
        bool _hasErrors = false;
        bool _hasWarnings = false;
    };

}
