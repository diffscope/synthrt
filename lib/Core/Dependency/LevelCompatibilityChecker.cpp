#include <synthrt/Core/Dependency/LevelCompatibilityChecker.h>

// TODO: PackageManager not yet migrated; include was unused in original implementation.
// #include <LangCore/Core/PackageManager.h>

#include <algorithm>
#include <sstream>

namespace srt::dependency {

    bool LevelCompatibilityChecker::ValidationResult::isInSupportedRange() const {
        int effectiveMax = systemMaximum > 0 ? systemMaximum : pluginLevel;
        return pluginLevel >= systemMinimum && pluginLevel <= effectiveMax;
    }

    LevelCompatibilityChecker::ValidationResult
    LevelCompatibilityChecker::checkCorePlugin(int pluginLevel, const LevelConfig &config) {
        ValidationResult result;
        result.pluginLevel = pluginLevel;
        result.systemMinimum = config.minimumLevel;
        result.systemMaximum = config.maximumLevel;

        int effectiveMax = config.getEffectiveMaximumLevel();

        if (pluginLevel < config.minimumLevel) {
            result.isCompatible = false;
            result.message = std::string("Plugin Level ") + std::to_string(pluginLevel) +
                " is below minimum supported Level " + std::to_string(config.minimumLevel);
            result.suggestion = std::string("Update plugin to Level ") + std::to_string(config.minimumLevel) + " or higher";
        } else if (pluginLevel > effectiveMax) {
            result.isCompatible = false;
            result.message = std::string("Plugin Level ") + std::to_string(pluginLevel) +
                " exceeds maximum supported Level " + std::to_string(effectiveMax);
            result.suggestion = std::string("Update system to support Level ") + std::to_string(pluginLevel) +
                " or use plugin with Level " + std::to_string(effectiveMax) + " or lower";
        } else {
            result.isCompatible = true;
            result.message = std::string("Plugin Level ") + std::to_string(pluginLevel) +
                " is compatible with system (supported range: " + std::to_string(config.minimumLevel) + "-" +
                std::to_string(effectiveMax) + ")";
            result.suggestion = "";
        }

        return result;
    }

    LevelCompatibilityChecker::ValidationResult
    LevelCompatibilityChecker::checkDependencyPlugin(int pluginLevel, const LevelConfig &config) {
        // 依赖插件的检查逻辑与核心插件相同
        return checkCorePlugin(pluginLevel, config);
    }

    std::vector<LevelCompatibilityChecker::ValidationResult>
    LevelCompatibilityChecker::checkAll(const std::vector<std::pair<std::string, int>> &pluginLevels,
                                        const std::vector<std::pair<std::string, int>> &dependencyLevels,
                                        const LevelConfig &config) {
        std::vector<ValidationResult> results;

        // 检查所有插件
        for (const auto &[pluginId, level] : pluginLevels) {
            auto result = checkCorePlugin(level, config);
            results.push_back(result);
        }

        // 检查所有依赖
        for (const auto &[depId, level] : dependencyLevels) {
            auto result = checkDependencyPlugin(level, config);
            results.push_back(result);
        }

        return results;
    }

    std::string LevelCompatibilityChecker::generateReport(const std::vector<ValidationResult> &results) {
        std::ostringstream oss;

        int compatibleCount = 0;
        int incompatibleCount = 0;

        for (const auto &result : results) {
            if (result.isCompatible) {
                compatibleCount++;
            } else {
                incompatibleCount++;
            }
        }

        oss << "=== Level Compatibility Check Report ===\n";
        oss << "Total checks: " << results.size() << "\n";
        oss << "Compatible: " << compatibleCount << "\n";
        oss << "Incompatible: " << incompatibleCount << "\n\n";

        oss << "=== Details ===\n";
        for (size_t i = 0; i < results.size(); ++i) {
            const auto &result = results[i];
            int effectiveMax = result.systemMaximum > 0 ? result.systemMaximum : result.pluginLevel;
            oss << "Check #" << (i + 1) << ":\n";
            oss << "  Plugin Level: " << result.pluginLevel << "\n";
            oss << "  System Range: " << result.systemMinimum << "-" << effectiveMax << "\n";
            oss << "  Status: " << (result.isCompatible ? "COMPATIBLE" : "INCOMPATIBLE") << "\n";
            oss << "  Message: " << result.message << "\n";
            if (!result.suggestion.empty()) {
                oss << "  Suggestion: " << result.suggestion << "\n";
            }
            oss << "\n";
        }

        return oss.str();
    }

} // namespace srt::dependency
