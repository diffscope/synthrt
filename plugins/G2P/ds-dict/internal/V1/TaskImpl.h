#pragma once

#include <synthrt/G2P/Task/DictTask.h>
#include <synthrt/G2P/Task/VersionedTaskImplBase.h>
#include <synthrt/Core/Module/Module.h>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace srt::g2p::plugins::DsDict::Internal::V1 {
    /// Dictionary entry with value (phoneme string etc.)
    struct DictEntry {
        std::string value;

        DictEntry() = default;
        explicit DictEntry(std::string v) : value(std::move(v)) {}
    };

    /// Dictionary container with hash-based lookup and shared_mutex for concurrent reads.
    class Dictionary {
    public:
        Dictionary() = default;
        explicit Dictionary(std::string id, std::string canonicalPath)
            : m_id(std::move(id)), m_canonicalPath(std::move(canonicalPath)) {}

        /// Lookup a key in the dictionary
        bool lookup(const std::string &key, std::string &value) const;

        /// Check if key exists
        bool contains(const std::string &key) const;

        /// Get dictionary ID
        const std::string &id() const { return m_id; }

        /// Get the canonical file path this dictionary was loaded from
        const std::string &canonicalPath() const { return m_canonicalPath; }

        /// Get entry count
        size_t size() const;

    private:
        std::string m_id;
        std::string m_canonicalPath;
        std::unordered_map<std::string, DictEntry> m_entries;
        mutable std::shared_mutex m_mutex;

        friend class DsDictTaskImpl;
    };

    /// DsDictTaskImpl - V1 implementation of DsDictTask
    class DsDictTaskImpl final : public srt::g2p::VersionedTaskImplBase {
    public:
        explicit DsDictTaskImpl(const srt::g2p::ModuleSpec *spec);
        ~DsDictTaskImpl() override = default;

        srt::core::Expected<void> initialize() override;

        srt::core::Expected<srt::core::NO<srt::g2p::TaskResult>>
        start(const srt::core::NO<srt::g2p::TaskInput> &input) override;

        std::string getConfig() const override;

    private:
        const srt::g2p::ModuleSpec *m_spec;

        /// Dictionary storage: dictId -> Dictionary
        std::unordered_map<std::string, std::shared_ptr<Dictionary>> m_dictionaries;
        mutable std::shared_mutex m_dictMutex;

        /// Dedup: canonical file path -> already-loaded Dictionary (avoids re-parsing
        /// the same file when multiple dictIds reference the same physical file).
        static std::unordered_map<std::string, std::weak_ptr<Dictionary>> s_loadedFiles;
        static std::mutex s_loadedFilesMutex;

        /// Load dictionary from file; deduplicates by canonical path.
        srt::core::Expected<void> loadDictionary(const std::string &dictId,
                                                 const std::filesystem::path &path);

        /// Get dictionary by id (thread-safe read)
        std::shared_ptr<Dictionary> getDictionary(const std::string &dictId) const;

        /// Process query input
        srt::core::Expected<srt::core::NO<srt::g2p::TaskResult>>
        processQuery(const srt::g2p::DictInputV1 &input) const;
    };

}
