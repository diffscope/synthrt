#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <synthrt/G2P/srt_g2p_global.h>
#include <synthrt/G2P/Task/Task.h>

namespace srt::g2p {

    /// DictInputV1 - Dictionary query input for V1 API.
    ///
    /// Migrated from LangCore::DictInputV1. Used as the input type for
    /// dictionary lookup tasks (e.g. DsDict plugin).
    class SRT_G2P_EXPORT DictInputV1 : public TaskInput {
    public:
        DictInputV1();

        /// Dictionary ID to query.
        std::string dictId;

        /// Keys to look up in the dictionary (single or multiple).
        std::vector<std::string> keys;

        /// Optional default value if a key is not found.
        std::string defaultValue;

        /// Optional flags for query behavior.
        uint32_t flags = 0;
    };

    /// DictResV1 - Dictionary query result for V1 API.
    ///
    /// Migrated from LangCore::DictResV1. Returned by dictionary lookup tasks.
    class SRT_G2P_EXPORT DictResV1 : public TaskResult {
    public:
        DictResV1();

        /// Query result values (one per input key).
        std::vector<std::string> values;

        /// Whether all keys were found.
        bool found = false;

        /// Number of keys found.
        size_t foundCount = 0;
    };

} // namespace srt::g2p
