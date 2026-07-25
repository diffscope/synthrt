#include <inferutil/Verifier.h>

#include <synthrt/Core/Support/Expected.h>
#include <fstream>
#include <sstream>

namespace srt::g2p::plugins::InferUtil
{

    IVerify::IVerify(VerifyEntry entry) : m_entry(std::move(entry)) {}
    IVerify::~IVerify() = default;

    VerifyRegex::VerifyRegex(const VerifyEntry &entry) : IVerify(entry) {
        m_regexOptions.set_encoding(RE2::Options::EncodingUTF8);
        m_regexOptions.set_log_errors(true);
        m_regexOptions.set_max_mem(8 << 20); // 8MB
    }

    VerifyRegex::~VerifyRegex() = default;

    srt::core::Expected<void> VerifyRegex::init() {
        std::string pattern = mergePatterns(m_entry.value);
        m_regex = std::make_unique<RE2>(pattern, m_regexOptions);
        if (!m_regex->ok()) {
            return srt::g2p::Error(srt::g2p::Error::ConfigError, "Invalid regex pattern: " + m_regex->error());
        }
        return {};
    }

    void VerifyRegex::verify(std::vector<VerifyRes> &input) {
        for (auto &[lyric, mode, error] : input) {
            if (RE2::FullMatch(lyric, *m_regex)) {
                mode = m_entry.mode;
                error = false;
            }
        }
    }

    std::string VerifyRegex::mergePatterns(const std::vector<std::string> &patterns) {
        if (patterns.empty())
            return "";
        std::ostringstream oss;
        oss << patterns[0];
        for (size_t i = 1; i < patterns.size(); ++i)
            oss << "|" << patterns[i];
        return oss.str();
    }

    VerifyArray::VerifyArray(const VerifyEntry &entry) : IVerify(entry) {
        m_array = std::set(m_entry.value.begin(), m_entry.value.end());
    }

    VerifyArray::~VerifyArray() = default;

    void VerifyArray::verify(std::vector<VerifyRes> &input) {
        for (auto &[lyric, mode, error] : input) {
            if (m_array.find(lyric) != m_array.end()) {
                mode = m_entry.mode;
                error = false;
            }
        }
    }

    VerifyDict::VerifyDict(const VerifyEntry &entry) : VerifyArray(entry) {}

    VerifyDict::~VerifyDict() = default;

    srt::core::Expected<void> VerifyDict::init() {
        auto wordsExp = loadWordsFromTxtFiles({m_entry.value.rbegin(), m_entry.value.rend()});
        if (!wordsExp) {
            return wordsExp.takeError();
        }
        m_array = wordsExp.take();
        return {};
    }

    srt::core::Expected<std::set<std::string>> VerifyDict::loadWordsFromTxtFiles(const std::vector<std::string> &paths) {
        std::set<std::string> words;

        for (const auto &path : paths) {
            if (!std::filesystem::exists(path)) {
                return srt::g2p::Error(srt::g2p::Error::ConfigError, "Dictionary file not found: " + path);
            }

            std::ifstream file(path);
            if (!file.is_open()) {
                return srt::g2p::Error(srt::g2p::Error::ConfigError, "Failed to open dictionary file: " + path);
            }

            std::string line;
            while (std::getline(file, line)) {
                if (line.empty())
                    continue;
                if (const size_t tab_pos = line.find('\t'); tab_pos != std::string::npos) {
                    if (std::string word = line.substr(0, tab_pos); !word.empty())
                        words.insert(word);
                }
            }
            file.close();
        }
        return words;
    }

    srt::core::Expected<std::unique_ptr<Verifier>> Verifier::Create(const std::vector<VerifyEntry> &entries) {
        auto verifier = std::unique_ptr<Verifier>(new Verifier());
        for (const auto &entry : entries) {
            std::unique_ptr<IVerify> v;
            if (entry.type == "regex") {
                v = std::make_unique<VerifyRegex>(entry);
            } else if (entry.type == "array") {
                v = std::make_unique<VerifyArray>(entry);
            } else if (entry.type == "dict") {
                v = std::make_unique<VerifyDict>(entry);
            } else {
                return srt::g2p::Error(srt::g2p::Error::ConfigError, "Unknown verifier type: " + entry.type);
            }

            if (auto initExp = v->init(); !initExp) {
                return initExp.takeError();
            }

            verifier->m_verifiers.push_back(std::move(v));
        }
        return verifier;
    }

    std::vector<VerifyRes> Verifier::verify(const std::vector<std::string> &input) const {
        std::vector<VerifyRes> result;
        result.reserve(input.size());
        for (const auto &lyric : input)
            result.emplace_back(VerifyRes{lyric, srt::g2p::kG2pModeCopy, true});

        for (const auto &verifier : m_verifiers)
            verifier->verify(result);
        return result;
    }

} // namespace srt::g2p::plugins::InferUtil
