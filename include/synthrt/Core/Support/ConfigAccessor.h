#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <synthrt/Core/srt_core_global.h>
#include <synthrt/Core/Module/Module.h>
#include <synthrt/Core/Support/Error.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Core/Support/JSON.h>

namespace srt::core {

    /// ConfigAccessor - 提供简洁的配置访问接口
    ///
    /// 这个类简化了插件配置的获取方式，提供一行代码获取配置的能力，
    /// 同时保持类型安全和错误处理。
    ///
    /// 使用示例：
    /// \code
    /// auto cfg = srt::core::config(spec());
    ///
    /// // 必需字段
    /// auto regexes = cfg.getArray<std::string>("regexes");
    /// if (!regexes) {
    ///     return regexes.takeError();
    /// }
    ///
    /// // 可选字段，带默认值
    /// auto enable = cfg.getBool("enable", false);
    /// \endcode
    class SRT_CORE_EXPORT ConfigAccessor;

    /// ValidationChain - 验证链
    ///
    /// 支持链式调用多个验证规则，返回第一个失败的错误
    class SRT_CORE_EXPORT ValidationChain {
    public:
        ValidationChain() = default;

        /// 添加整数值范围验证
        /// @param value 要验证的值
        /// @param min 最小值（包含）
        /// @param max 最大值（包含）
        /// @param key 配置键名（用于错误消息）
        /// @return 返回自身，支持链式调用
        ValidationChain &validateIntRange(int value, int min, int max, const std::string &key = "");

        /// 添加双精度浮点数值范围验证
        /// @param value 要验证的值
        /// @param min 最小值（包含）
        /// @param max 最大值（包含）
        /// @param key 配置键名（用于错误消息）
        /// @return 返回自身，支持链式调用
        ValidationChain &validateDoubleRange(double value, double min, double max, const std::string &key = "");

        /// 添加字符串值验证
        /// @param value 要验证的值
        /// @param allowedValues 允许的值集合
        /// @param key 配置键名（用于错误消息）
        /// @return 返回自身，支持链式调用
        ValidationChain &validateStringAllowed(const std::string &value,
                                               const std::vector<std::string> &allowedValues,
                                               const std::string &key = "");

        /// 添加字符串数组非空验证
        /// @param value 要验证的值
        /// @param key 配置键名（用于错误消息）
        /// @return 返回自身，支持链式调用
        ValidationChain &validateArrayNotEmpty(const std::vector<std::string> &value, const std::string &key = "");

        /// 添加自定义验证
        /// @param validator 验证函数，返回 Expected<bool>
        /// @return 返回自身，支持链式调用
        ValidationChain &validate(std::function<Expected<bool>()> validator);

        /// 执行验证
        /// @return 成功返回 true，失败返回第一个错误
        Expected<bool> execute() const;

        /// 检查是否有错误
        /// @return 如果有错误返回 true
        bool hasError() const { return _error.has_value(); }

        /// 获取错误
        /// @return 错误对象，如果没有错误则返回默认值
        Error error() const { return _error.value_or(Error::success()); }

    private:
        std::optional<Error> _error;
    };

    class SRT_CORE_EXPORT ConfigAccessor {
    public:
        /// 从 ModuleSpec 创建配置访问器
        explicit ConfigAccessor(const ModuleSpec *spec);

        /// 从 JsonObject 创建配置访问器
        explicit ConfigAccessor(const JsonObject &config, const std::filesystem::path &basePath = {});

        // ==================== 必需字段 ====================

        /// 获取必需的字符串
        /// @param key 配置键名
        /// @return 成功返回字符串，失败返回错误
        Expected<std::string> getString(const std::string &key) const;

        /// 获取必需的整数
        /// @param key 配置键名
        /// @return 成功返回整数，失败返回错误
        Expected<int> getInt(const std::string &key) const;

        /// 获取必需的双精度浮点数
        /// @param key 配置键名
        /// @return 成功返回双精度浮点数，失败返回错误
        Expected<double> getDouble(const std::string &key) const;

        /// 获取必需的布尔值
        /// @param key 配置键名
        /// @return 成功返回布尔值，失败返回错误
        Expected<bool> getBool(const std::string &key) const;

        /// 获取必需的路径（相对于模块路径解析）
        /// @param key 配置键名
        /// @return 成功返回路径，失败返回错误
        Expected<std::filesystem::path> getPath(const std::string &key) const;

        /// 获取配置中的路径并解析为规范化绝对路径。
        /// 内部调用 canonical() 并回退 absolute()。
        /// @param key 配置键名
        /// @return 规范化后的绝对路径；键缺失或路径无效时返回错误
        Expected<std::filesystem::path> getResolvedPath(const std::string &key) const;

        /// 获取配置中的路径并相对于指定 basePath 解析为规范化绝对路径。
        /// @param key 配置键名
        /// @param basePath 解析相对路径时使用的基础路径（覆盖默认的模块路径）
        /// @return 规范化后的绝对路径；键缺失或路径无效时返回错误
        Expected<std::filesystem::path> getResolvedPath(const std::string &key,
                                                        const std::filesystem::path &basePath) const;

        /// 获取必需的字符串数组
        /// @param key 配置键名
        /// @return 成功返回字符串数组，失败返回错误
        Expected<std::vector<std::string>> getStringArray(const std::string &key) const;

        // ==================== 可选字段 ====================

        /// 获取可选的字符串，带默认值
        std::string getString(const std::string &key, const std::string &defaultValue) const;

        /// 获取可选的整数，带默认值
        int getInt(const std::string &key, int defaultValue) const;

        /// 获取可选的双精度浮点数，带默认值
        double getDouble(const std::string &key, double defaultValue) const;

        /// 获取可选的布尔值，带默认值
        bool getBool(const std::string &key, bool defaultValue) const;

        /// 获取可选的路径，带默认值
        std::filesystem::path getPath(const std::string &key, const std::filesystem::path &defaultValue) const;

        /// 获取可选的字符串数组，带默认值
        std::vector<std::string> getStringArray(const std::string &key,
                                                 const std::vector<std::string> &defaultValue) const;

        // ==================== 辅助方法 ====================

        /// 检查键是否存在
        bool has(const std::string &key) const;

        /// 获取原始 JSON 对象
        const JsonObject &raw() const { return m_config; }

        /// 获取基础路径
        const std::filesystem::path &basePath() const { return m_basePath; }

        // ==================== 范围验证 ====================

        /// 验证整数值在指定范围内
        /// @param value 要验证的值
        /// @param min 最小值（包含）
        /// @param max 最大值（包含）
        /// @param key 配置键名（用于错误消息）
        /// @return 成功返回 true，失败返回错误
        static Expected<bool> validateIntRange(int value, int min, int max, const std::string &key = "");

        /// 验证双精度浮点数值在指定范围内
        /// @param value 要验证的值
        /// @param min 最小值（包含）
        /// @param max 最大值（包含）
        /// @param key 配置键名（用于错误消息）
        /// @return 成功返回 true，失败返回错误
        static Expected<bool> validateDoubleRange(double value, double min, double max, const std::string &key = "");

        /// 验证字符串值在允许的集合中
        /// @param value 要验证的值
        /// @param allowedValues 允许的值集合
        /// @param key 配置键名（用于错误消息）
        /// @return 成功返回 true，失败返回错误
        static Expected<bool> validateStringAllowed(const std::string &value,
                                                    const std::vector<std::string> &allowedValues,
                                                    const std::string &key = "");

        /// 验证字符串数组不为空
        /// @param value 要验证的值
        /// @param key 配置键名（用于错误消息）
        /// @return 成功返回 true，失败返回错误
        static Expected<bool> validateArrayNotEmpty(const std::vector<std::string> &value, const std::string &key = "");

    private:
        const JsonObject &m_config;
        std::filesystem::path m_basePath;
    };

    /// 便捷函数：从 ModuleSpec 创建配置访问器
    inline ConfigAccessor config(const ModuleSpec *spec) { return ConfigAccessor(spec); }

} // namespace srt::core
