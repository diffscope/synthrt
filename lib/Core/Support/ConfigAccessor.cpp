#include <synthrt/Core/Support/ConfigAccessor.h>

#include <stdcorelib/path.h>
#include <stdcorelib/str.h>

namespace srt::core {

    // ==================== 辅助函数 ====================
    // srt::core::Error only supports a 2-arg constructor (type, msg) without suggestion.
    // The original LangCore::Error had a 3-arg constructor (type, msg, suggestion).
    // To preserve all information, the suggestion is appended to the message.
    // Error type mapping (per docs/refactoring-v3/07-engineering-conventions.md §2.2):
    //   Error::ConfigError     -> Error::InvalidFormat
    //   Error::ValidationError -> Error::InvalidArgument

    namespace {
        Error configError(const std::string &msg, const std::string &suggestion) {
            std::string full = suggestion.empty() ? msg : msg + " (suggestion: " + suggestion + ")";
            return Error(Error::InvalidFormat, std::move(full));
        }

        Error validationError(const std::string &msg, const std::string &suggestion) {
            std::string full = suggestion.empty() ? msg : msg + " (suggestion: " + suggestion + ")";
            return Error(Error::InvalidArgument, std::move(full));
        }
    }

    ConfigAccessor::ConfigAccessor(const ModuleSpec *spec) :
        m_config(spec->manifestConfiguration()), m_basePath(spec->path()) {
    }

    ConfigAccessor::ConfigAccessor(const JsonObject &config, const std::filesystem::path &basePath) :
        m_config(config), m_basePath(basePath) {
    }

    // ==================== 辅助模板函数 ====================

    namespace {
        // 模板函数：检查必需字段是否存在
        template<typename T>
        Expected<JsonObject::const_iterator> checkRequiredField(const JsonObject &config, const std::string &key) {
            auto it = config.find(key);
            if (it == config.end()) {
                return configError("Missing required field: " + key,
                                   "Add the '" + key + "' field to the configuration");
            }
            return it;
        }

        // 模板函数：检查字段类型是否匹配
        template<typename CheckFunc>
        Expected<void> checkFieldType(const std::string &key, const JsonValue &value, CheckFunc typeCheck, const char *typeName) {
            if (!typeCheck(value)) {
                return configError("Field '" + key + "' must be a " + typeName,
                                   "Change the value of '" + key + "' to a " + typeName + " type");
            }
            return {};
        }
    }

    // ==================== 必需字段 ====================

    Expected<std::string> ConfigAccessor::getString(const std::string &key) const {
        auto itExp = checkRequiredField<std::string>(m_config, key);
        if (!itExp) {
            return itExp.takeError();
        }
        auto it = itExp.take();
        auto typeCheckExp = checkFieldType(key, it->second, [](const JsonValue& v) { return v.isString(); }, "string");
        if (!typeCheckExp) {
            return typeCheckExp.takeError();
        }
        return it->second.toString();
    }

    Expected<int> ConfigAccessor::getInt(const std::string &key) const {
        auto itExp = checkRequiredField<int>(m_config, key);
        if (!itExp) {
            return itExp.takeError();
        }
        auto it = itExp.take();
        auto typeCheckExp = checkFieldType(key, it->second, [](const JsonValue& v) { return v.isNumber(); }, "integer");
        if (!typeCheckExp) {
            return typeCheckExp.takeError();
        }
        return static_cast<int>(it->second.toInt());
    }

    Expected<double> ConfigAccessor::getDouble(const std::string &key) const {
        auto itExp = checkRequiredField<double>(m_config, key);
        if (!itExp) {
            return itExp.takeError();
        }
        auto it = itExp.take();
        auto typeCheckExp = checkFieldType(key, it->second, [](const JsonValue& v) { return v.isNumber(); }, "number");
        if (!typeCheckExp) {
            return typeCheckExp.takeError();
        }
        return it->second.toDouble();
    }

    Expected<bool> ConfigAccessor::getBool(const std::string &key) const {
        auto itExp = checkRequiredField<bool>(m_config, key);
        if (!itExp) {
            return itExp.takeError();
        }
        auto it = itExp.take();
        auto typeCheckExp = checkFieldType(key, it->second, [](const JsonValue& v) { return v.isBool(); }, "boolean");
        if (!typeCheckExp) {
            return typeCheckExp.takeError();
        }
        return it->second.toBool();
    }

    Expected<std::filesystem::path> ConfigAccessor::getPath(const std::string &key) const {
        auto strExp = getString(key);
        if (!strExp) {
            return strExp.takeError();
        }
        // 相对于模块路径解析
        return stdc::path::clean_path(m_basePath / stdc::path::from_utf8(*strExp));
    }

    Expected<std::filesystem::path> ConfigAccessor::getResolvedPath(const std::string &key) const {
        return getResolvedPath(key, m_basePath);
    }

    Expected<std::filesystem::path> ConfigAccessor::getResolvedPath(const std::string &key,
                                                                    const std::filesystem::path &basePath) const {
        auto strExp = getString(key);
        if (!strExp) {
            return strExp.takeError();
        }
        auto path = stdc::path::clean_path(basePath / stdc::path::from_utf8(strExp.take()));

        std::error_code ec;
        auto canonical = std::filesystem::canonical(path, ec);
        if (!ec) {
            return canonical;
        }
        // Fallback: use absolute() if canonical() fails (e.g. path does not exist yet)
        return std::filesystem::absolute(path);
    }

    Expected<std::vector<std::string>> ConfigAccessor::getStringArray(const std::string &key) const {
        auto itExp = checkRequiredField<std::vector<std::string>>(m_config, key);
        if (!itExp) {
            return itExp.takeError();
        }
        auto it = itExp.take();
        auto typeCheckExp = checkFieldType(key, it->second, [](const JsonValue& v) { return v.isArray(); }, "array");
        if (!typeCheckExp) {
            return typeCheckExp.takeError();
        }

        std::vector<std::string> result;
        const auto &arr = it->second.toArray();
        result.reserve(arr.size());

        for (size_t i = 0; i < arr.size(); ++i) {
            if (!arr[i].isString()) {
                return configError("Array element #" + std::to_string(i) + " of '" + key + "' must be string",
                                   "Ensure all elements in the '" + key + "' array are strings");
            }
            result.push_back(arr[i].toString());
        }
        return result;
    }

    // ==================== 可选字段 ====================

    std::string ConfigAccessor::getString(const std::string &key, const std::string &defaultValue) const {
        auto it = m_config.find(key);
        if (it == m_config.end() || !it->second.isString()) {
            return defaultValue;
        }
        return it->second.toString();
    }

    int ConfigAccessor::getInt(const std::string &key, int defaultValue) const {
        auto it = m_config.find(key);
        if (it == m_config.end() || !it->second.isNumber()) {
            return defaultValue;
        }
        return static_cast<int>(it->second.toInt());
    }

    double ConfigAccessor::getDouble(const std::string &key, double defaultValue) const {
        auto it = m_config.find(key);
        if (it == m_config.end() || !it->second.isNumber()) {
            return defaultValue;
        }
        return it->second.toDouble();
    }

    bool ConfigAccessor::getBool(const std::string &key, bool defaultValue) const {
        auto it = m_config.find(key);
        if (it == m_config.end() || !it->second.isBool()) {
            return defaultValue;
        }
        return it->second.toBool();
    }

    std::filesystem::path ConfigAccessor::getPath(const std::string &key,
                                                   const std::filesystem::path &defaultValue) const {
        auto it = m_config.find(key);
        if (it == m_config.end() || !it->second.isString()) {
            return defaultValue;
        }
        return stdc::path::clean_path(m_basePath / stdc::path::from_utf8(it->second.toString()));
    }

    std::vector<std::string> ConfigAccessor::getStringArray(const std::string &key,
                                                             const std::vector<std::string> &defaultValue) const {
        auto it = m_config.find(key);
        if (it == m_config.end() || !it->second.isArray()) {
            return defaultValue;
        }

        std::vector<std::string> result;
        const auto &arr = it->second.toArray();
        result.reserve(arr.size());

        for (const auto &item : arr) {
            if (item.isString()) {
                result.push_back(item.toString());
            }
        }
        return result;
    }

    // ==================== 辅助方法 ====================

    bool ConfigAccessor::has(const std::string &key) const { return m_config.find(key) != m_config.end(); }

    // ==================== 范围验证 ====================

    Expected<bool> ConfigAccessor::validateIntRange(int value, int min, int max, const std::string &key) {
        if (value < min || value > max) {
            std::string keyMsg = key.empty() ? "" : "'" + key + "' ";
            return validationError(
                stdc::formatN("Value %1%2is out of range [%3, %4]", keyMsg, value, min, max),
                "Adjust the value to be within the valid range");
        }
        return true;
    }

    Expected<bool> ConfigAccessor::validateDoubleRange(double value, double min, double max, const std::string &key) {
        if (value < min || value > max) {
            std::string keyMsg = key.empty() ? "" : "'" + key + "' ";
            return validationError(
                stdc::formatN("Value %1%2is out of range [%3, %4]", keyMsg, value, min, max),
                "Adjust the value to be within the valid range");
        }
        return true;
    }

    Expected<bool> ConfigAccessor::validateStringAllowed(const std::string &value,
                                                          const std::vector<std::string> &allowedValues,
                                                          const std::string &key) {
        for (const auto &allowed : allowedValues) {
            if (value == allowed) {
                return true;
            }
        }

        std::string keyMsg = key.empty() ? "" : "'" + key + "' ";
        std::string allowedList;
        for (size_t i = 0; i < allowedValues.size(); ++i) {
            if (i > 0) {
                allowedList += ", ";
            }
            allowedList += "'" + allowedValues[i] + "'";
        }
        return validationError(
            stdc::formatN("Value %1%2is not allowed. Allowed values: %3", keyMsg, value, allowedList),
            "Use one of the allowed values");
    }

    Expected<bool> ConfigAccessor::validateArrayNotEmpty(const std::vector<std::string> &value, const std::string &key) {
        if (value.empty()) {
            std::string keyMsg = key.empty() ? "" : "'" + key + "' ";
            return validationError(
                stdc::formatN("Array %1must not be empty", keyMsg),
                "Add at least one element to the array");
        }
        return true;
    }

    // ==================== ValidationChain 实现 ====================

    ValidationChain &ValidationChain::validateIntRange(int value, int min, int max, const std::string &key) {
        if (!_error) {
            auto result = ConfigAccessor::validateIntRange(value, min, max, key);
            if (!result) {
                _error = result.takeError();
            }
        }
        return *this;
    }

    ValidationChain &ValidationChain::validateDoubleRange(double value, double min, double max, const std::string &key) {
        if (!_error) {
            auto result = ConfigAccessor::validateDoubleRange(value, min, max, key);
            if (!result) {
                _error = result.takeError();
            }
        }
        return *this;
    }

    ValidationChain &ValidationChain::validateStringAllowed(const std::string &value,
                                                            const std::vector<std::string> &allowedValues,
                                                            const std::string &key) {
        if (!_error) {
            auto result = ConfigAccessor::validateStringAllowed(value, allowedValues, key);
            if (!result) {
                _error = result.takeError();
            }
        }
        return *this;
    }

    ValidationChain &ValidationChain::validateArrayNotEmpty(const std::vector<std::string> &value, const std::string &key) {
        if (!_error) {
            auto result = ConfigAccessor::validateArrayNotEmpty(value, key);
            if (!result) {
                _error = result.takeError();
            }
        }
        return *this;
    }

    ValidationChain &ValidationChain::validate(std::function<Expected<bool>()> validator) {
        if (!_error) {
            auto result = validator();
            if (!result) {
                _error = result.takeError();
            }
        }
        return *this;
    }

    Expected<bool> ValidationChain::execute() const {
        if (_error) {
            return *_error;
        }
        return true;
    }

} // namespace srt::core
