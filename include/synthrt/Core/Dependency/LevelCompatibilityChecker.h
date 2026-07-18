#pragma once

#include <synthrt/Core/srt_core_global.h>
#include <synthrt/Core/Support/Expected.h>

#include <string>
#include <vector>

namespace srt::dependency {

    /// LevelCompatibilityChecker - Level 兼容性检查器
    /// 用于检查插件和依赖的 Level 兼容性
    /// Level 是 API 兼容性的唯一标准
    class SRT_CORE_EXPORT LevelCompatibilityChecker {
    public:
        /// Level 配置
        struct LevelConfig {
            int currentLevel;   /// 系统当前 Level
            int minimumLevel;   /// 系统最小支持 Level
            int maximumLevel;   /// 系统最大支持 Level（0 表示无限制，使用 currentLevel）

            LevelConfig(int current = 1, int minimum = 1, int maximum = 1)
                : currentLevel(current), minimumLevel(minimum), maximumLevel(maximum) {}

            /// 获取有效的最大 Level（考虑 maximumLevel=0 的情况）
            int getEffectiveMaximumLevel() const {
                return maximumLevel > 0 ? maximumLevel : currentLevel;
            }
        };

        /// 验证结果
        struct SRT_CORE_EXPORT ValidationResult {
            bool isCompatible;  /// 是否兼容
            int pluginLevel;    /// 插件 Level
            int systemMinimum;  /// 系统最小 Level
            int systemMaximum;  /// 系统最大 Level
            std::string message;    /// 详细消息
            std::string suggestion; /// 建议

            /// 检查是否在支持范围内
            bool isInSupportedRange() const;
        };

        /// 检查核心插件的 Level 兼容性
        /// @param pluginLevel 插件 Level
        /// @param config Level 配置
        /// @return 验证结果
        static ValidationResult checkCorePlugin(int pluginLevel, const LevelConfig &config);

        /// 检查依赖插件的 Level 兼容性
        /// @param pluginLevel 依赖插件 Level
        /// @param config Level 配置
        /// @return 验证结果
        static ValidationResult checkDependencyPlugin(int pluginLevel, const LevelConfig &config);

        /// 批量检查所有插件和依赖
        /// @param pluginLevels 插件 Level 列表 {pluginId, level}
        /// @param dependencyLevels 依赖 Level 列表 {dependencyId, level}
        /// @param config Level 配置
        /// @return 所有验证结果
        static std::vector<ValidationResult>
        checkAll(const std::vector<std::pair<std::string, int>> &pluginLevels,
                 const std::vector<std::pair<std::string, int>> &dependencyLevels, const LevelConfig &config);

        /// 生成验证报告
        /// @param results 验证结果列表
        /// @return 格式化的报告字符串
        static std::string generateReport(const std::vector<ValidationResult> &results);
    };

} // namespace srt::dependency
