#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace ds::infer::inferutil {
    class ErrorCollector {
    public:
        ErrorCollector() = default;
        ~ErrorCollector() = default;

        void collectError(const char *msg) {
            m_errors.emplace_back(msg);
        }

        void collectError(const std::string &msg) {
            m_errors.emplace_back(msg);
        }

        void collectError(std::string &&msg) {
            m_errors.emplace_back(std::move(msg));
        }

        bool hasErrors() const {
            return !m_errors.empty();
        }

        const std::vector<std::string> &errors() const {
            return m_errors;
        }

        inline std::string getErrorMessage(const std::string &msgPrefix) const;

        void clear() {
            m_errors.clear();
        }

    private:
        std::vector<std::string> m_errors;
    };

    inline std::string ErrorCollector::getErrorMessage(const std::string &msgPrefix) const {
        if (m_errors.empty()) {
            return {};
        }
        const std::string middlePart = " (";
        const std::string countSuffix =
            (m_errors.size() == 1) ? " error found):\n" : " errors found):\n";

        size_t totalLength = msgPrefix.size() + middlePart.size() +
                             std::to_string(m_errors.size()).size() + countSuffix.size();

        for (size_t i = 0; i < m_errors.size(); ++i) {
            totalLength += std::to_string(i + 1).size() + 2; // index + ". "
            totalLength += m_errors[i].size();
            if (i != m_errors.size() - 1) {
                totalLength += 2; // "; "
            }
        }

        std::string result;
        result.reserve(totalLength);

        result.append(msgPrefix);
        result.append(middlePart);
        result.append(std::to_string(m_errors.size()));
        result.append(countSuffix);

        for (size_t i = 0; i < m_errors.size(); ++i) {
            result.append(std::to_string(i + 1));
            result.append(". ");
            result.append(m_errors[i]);
            if (i != m_errors.size() - 1) {
                result.append(";\n");
            }
        }

        return result;
    }
}
