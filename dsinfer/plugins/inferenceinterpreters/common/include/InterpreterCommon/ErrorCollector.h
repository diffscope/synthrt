#ifndef DSINFER_INTERPRETER_COMMON_ERRORCOLLECTOR_H
#define DSINFER_INTERPRETER_COMMON_ERRORCOLLECTOR_H

#include <cstddef>
#include <string>
#include <vector>

namespace ds::InterpreterCommon {
    class ErrorCollector {
    public:
        ErrorCollector() = default;
        ~ErrorCollector() = default;

        void collectError(const char *msg) {
            _errors.emplace_back(msg);
        }

        void collectError(const std::string &msg) {
            _errors.emplace_back(msg);
        }

        void collectError(std::string &&msg) {
            _errors.emplace_back(std::move(msg));
        }

        bool hasErrors() const {
            return !_errors.empty();
        }

        const std::vector<std::string> &errors() const {
            return _errors;
        }

        inline std::string getErrorMessage(const std::string &msgPrefix) const;

        void clear() {
            _errors.clear();
        }

    private:
        std::vector<std::string> _errors;
    };

    inline std::string ErrorCollector::getErrorMessage(const std::string &msgPrefix) const {
        if (_errors.empty()) {
            return {};
        }
        const std::string middlePart = " (";
        const std::string countSuffix = " errors found):\n";

        size_t totalLength = msgPrefix.size() + middlePart.size() +
                             std::to_string(_errors.size()).size() + countSuffix.size();

        for (size_t i = 0; i < _errors.size(); ++i) {
            totalLength += std::to_string(i + 1).size() + 2; // index + ". "
            totalLength += _errors[i].size();
            if (i != _errors.size() - 1) {
                totalLength += 2; // "; "
            }
        }

        std::string result;
        result.reserve(totalLength);

        result.append(msgPrefix);
        result.append(middlePart);
        result.append(std::to_string(_errors.size()));
        result.append(countSuffix);

        for (size_t i = 0; i < _errors.size(); ++i) {
            result.append(std::to_string(i + 1));
            result.append(". ");
            result.append(_errors[i]);
            if (i != _errors.size() - 1) {
                result.append(";\n");
            }
        }

        return result;
    }
}

#endif // DSINFER_INTERPRETER_COMMON_ERRORCOLLECTOR_H