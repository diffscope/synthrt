#pragma once

#include <string>
#include <vector>

#include <synthrt/Core/Support/JSON.h>

#include <diffsinger/Bank/dsbank_global.h>

namespace ds::bank {

    /// JsonSchemaValidator - A minimal JSON Schema validation engine supporting
    /// the subset of draft-07 keywords used by DiffSinger package manifests:
    ///   - \c type (string, number, boolean, array, object)
    ///   - \c required (list of required property names)
    ///   - \c properties (nested object schema)
    ///   - \c items (array element schema)
    ///
    /// This is intentionally not a full JSON Schema implementation; it exists to
    /// validate the structural shape of \c package.json during package scanning.
    class DSBANK_EXPORT JsonSchemaValidator {
    public:
        /// Validates \p value against \p schema. On failure, appends one or more
        /// human-readable messages to \p errors. Returns \c true when the value
        /// conforms to the schema.
        bool validate(const srt::core::JsonValue &value, const srt::core::JsonObject &schema,
                      std::vector<std::string> &errors) const;
    };

}
