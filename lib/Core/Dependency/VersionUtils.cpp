#include <synthrt/Core/Dependency/VersionUtils.h>

#include <algorithm>
#include <map>
#include <sstream>
#include <utility>

// TODO: Re-enable logging when srt::core::Logger is migrated.
// #include <LangCore/Core/ManagerLogger.h>

namespace srt::dependency {

    // Helper: check if a string is a version-like pattern (digits and dots only)
    static bool isVersionString(const std::string &s) {
        if (s.empty())
            return false;
        for (char c : s) {
            if (!std::isdigit(c) && c != '.')
                return false;
        }
        return std::isdigit(s.front());
    }

    // Helper: try to parse "VERSION - VERSION" hyphen range without regex.
    // Returns true if parsed, fills lhs/rhs.
    static bool parseHyphenRange(const std::string &str, std::string &lhs, std::string &rhs) {
        // Find the '-' that is surrounded by optional whitespace and version strings.
        // We look for the first '-' that has a version on each side.
        auto dashPos = str.find('-');
        while (dashPos != std::string::npos) {
            // Trim left side
            auto leftEnd = dashPos;
            while (leftEnd > 0 && str[leftEnd - 1] == ' ')
                leftEnd--;
            std::string left = str.substr(0, leftEnd);

            // Trim right side
            auto rightStart = dashPos + 1;
            while (rightStart < str.size() && str[rightStart] == ' ')
                rightStart++;
            std::string right = str.substr(rightStart);

            if (isVersionString(left) && isVersionString(right)) {
                lhs = left;
                rhs = right;
                return true;
            }

            dashPos = str.find('-', dashPos + 1);
        }
        return false;
    }
    bool VersionRange::Constraint::matches(const std::string &testVersion) const {
        if (op == Op::ANY)
            return true;

        std::string normTest = normalizeVersion(testVersion);

        if (op == Op::HYPHEN_RANGE) {
            std::string normMin = normalizeVersion(version);
            std::string normMax = normalizeVersion(version2);

            int cmpMin = compareVersions(normTest, normMin);
            int cmpMax = compareVersions(normTest, normMax);

            return cmpMin >= 0 && cmpMax <= 0;
        }

        std::string normTarget = normalizeVersion(version);
        int cmp = compareVersions(normTest, normTarget);

        switch (op) {
        case Op::LESS:
            return cmp < 0;
        case Op::LESS_EQUAL:
            return cmp <= 0;
        case Op::GREATER:
            return cmp > 0;
        case Op::GREATER_EQUAL:
            return cmp >= 0;
        case Op::EQUAL:
            return cmp == 0;
        case Op::COMPATIBLE:
            {
                std::vector<std::string> targetParts;
                std::string part;
                std::istringstream iss(normTarget);
                while (std::getline(iss, part, '.'))
                    targetParts.push_back(part);

                std::vector<std::string> testParts;
                std::istringstream issTest(normTest);
                while (std::getline(issTest, part, '.'))
                    testParts.push_back(part);

                if (targetParts.size() >= 2) {
                    if (targetParts[0] != testParts[0] || targetParts[1] != testParts[1])
                        return false;
                    auto safeStoi = [](const std::string &s) -> int {
                        try { return std::stoi(s); }
                        catch (...) { return 0; }
                    };
                    int targetPatch = targetParts.size() > 2 ? safeStoi(targetParts[2]) : 0;
                    int testPatch = testParts.size() > 2 ? safeStoi(testParts[2]) : 0;
                    return testPatch >= targetPatch;
                }
                return cmp == 0;
            }
        default:
            return false;
        }
    }

    VersionRange::VersionRange(const std::string &rangeStr) {
        if (rangeStr.empty() || rangeStr == "*") {
            constraints_.push_back({Op::ANY, ""});
            return;
        }

        if (rangeStr.find('-') != std::string::npos) {
            std::string lhs, rhs;
            if (parseHyphenRange(rangeStr, lhs, rhs)) {
                constraints_.push_back({Op::HYPHEN_RANGE, lhs, rhs});
                return;
            }
        }

        if (isVersionString(rangeStr)) {
            constraints_.push_back({Op::EQUAL, rangeStr});
            return;
        }

        static const std::vector<std::pair<std::string, Op>> operators = {
            {"<=", Op::LESS_EQUAL}, {">=", Op::GREATER_EQUAL}, {"<", Op::LESS},      {">", Op::GREATER},
            {"==", Op::EQUAL},      {"=", Op::EQUAL},          {"~", Op::COMPATIBLE}};

        bool parsed = false;
        for (const auto &[opStr, opType] : operators) {
            if (rangeStr.find(opStr) == 0) {
                std::string version = rangeStr.substr(opStr.length());
                if (version.empty()) {
                    valid_ = false;
                    parseError_ = "empty version after operator '" + opStr + "'";
                    constraints_.push_back({Op::ANY, ""});
                    return;
                }
                constraints_.push_back({opType, version});
                parsed = true;
                break;
            }
        }

        if (!parsed) {
            std::istringstream iss(rangeStr);
            std::string constraintStr;
            while (iss >> constraintStr) {
                if (!constraintStr.empty()) {
                    auto constraint = parseConstraint(constraintStr);
                    if (constraint.op == Op::ANY && constraintStr != "*" && !constraintStr.empty()) {
                        valid_ = false;
                        parseError_ = "invalid constraint: " + constraintStr;
                    }
                    constraints_.push_back(constraint);
                }
            }

            if (constraints_.empty()) {
                valid_ = false;
                parseError_ = "no valid constraints in range";
                constraints_.push_back({Op::ANY, ""});
            }
        }
    }

    std::string VersionRange::Constraint::toString() const {
        switch (op) {
        case Op::LESS:
            return "<" + version;
        case Op::LESS_EQUAL:
            return "<=" + version;
        case Op::GREATER:
            return ">" + version;
        case Op::GREATER_EQUAL:
            return ">=" + version;
        case Op::EQUAL:
            return "==" + version;
        case Op::COMPATIBLE:
            return "~" + version;
        case Op::ANY:
            return "*";
        case Op::HYPHEN_RANGE:
            return version + "-" + version2;
        default:
            return "unknown";
        }
    }

    std::string VersionRange::toString() const {
        if (constraints_.empty())
            return "*";
        if (constraints_.size() == 1)
            return constraints_[0].toString();

        std::string result;
        for (size_t i = 0; i < constraints_.size(); ++i) {
            if (i > 0)
                result += " ";
            result += constraints_[i].toString();
        }
        return result;
    }

    bool VersionRange::Constraint::isMoreSpecificThan(const Constraint &other) const {
        if (op == Op::ANY)
            return false;
        if (other.op == Op::ANY)
            return true;
        if (op == Op::EQUAL && other.op != Op::EQUAL)
            return true;
        if (op == Op::COMPATIBLE && other.op == Op::HYPHEN_RANGE)
            return true;
        return false;
    }

    std::vector<std::string> VersionRange::getVersionsInRange(const std::vector<std::string> &availableVersions) const {
        std::vector<std::string> result;
        std::vector<std::string> sortedVersions = availableVersions;

        std::sort(sortedVersions.begin(), sortedVersions.end(),
                  [](const std::string &a, const std::string &b) { return compareVersions(a, b) > 0; });

        for (const auto &version : sortedVersions) {
            bool allMatch = true;
            for (const auto &constraint : constraints_) {
                if (!constraint.matches(version)) {
                    allMatch = false;
                    break;
                }
            }
            if (allMatch)
                result.push_back(version);
        }

        return result;
    }

    std::string VersionRange::normalizeVersion(const std::string &version) {
        if (version.empty())
            return "0.0.0";

        std::string normalized = version;
        if (!normalized.empty() && (normalized[0] == 'v' || normalized[0] == 'V')) {
            normalized = normalized.substr(1);
        }

        if (const size_t dashPos = normalized.find('-'); dashPos != std::string::npos) {
            normalized = normalized.substr(0, dashPos);
        }

        std::vector<std::string> parts;
        std::string part;
        std::istringstream iss(normalized);
        while (std::getline(iss, part, '.')) {
            std::string numPart;
            for (const char c : part) {
                if (std::isdigit(c))
                    numPart += c;
            }
            if (!numPart.empty())
                parts.push_back(numPart);
        }

        while (parts.size() < 3)
            parts.push_back("0");
        if (parts.size() > 3)
            parts.resize(3);

        return parts[0] + "." + parts[1] + "." + parts[2];
    }

    VersionRange::Constraint VersionRange::parseConstraint(const std::string &constraintStr) {
        if (std::all_of(constraintStr.begin(), constraintStr.end(),
                        [](const char c) { return std::isdigit(c) || c == '.'; })) {
            return {Op::EQUAL, constraintStr};
        }

        static std::vector<std::pair<std::string, Op>> operators = {
            {"<=", Op::LESS_EQUAL}, {">=", Op::GREATER_EQUAL}, {"<", Op::LESS}, {">", Op::GREATER},
            {"==", Op::EQUAL},      {"~", Op::COMPATIBLE},     {"=", Op::EQUAL}};

        for (const auto &[opStr, opType] : operators) {
            if (constraintStr.find(opStr) == 0) {
                const std::string version = constraintStr.substr(opStr.length());
                if (version.empty()) {
                    // 不抛出异常，返回 ANY 作为默认值
                    return {Op::ANY, ""};
                }
                return {opType, version};
            }
        }

        // 不抛出异常，返回 ANY 作为默认值
        return {Op::ANY, ""};
    }

    int VersionRange::compareVersions(const std::string &v1, const std::string &v2) {
        std::vector<int> parts1, parts2;

        auto parse = [](const std::string &v, std::vector<int> &parts)
        {
            std::string part;
            std::istringstream iss(v);
            while (std::getline(iss, part, '.')) {
                try {
                    parts.push_back(std::stoi(part));
                }
                catch (...) {
                    // Non-numeric version part treated as 0
                    // TODO: Re-enable logging when srt::core::Logger is migrated
                    // DependencyLog.langCoreWarning("Non-numeric version part '%1' in version string, treating as 0", part);
                    parts.push_back(0);
                }
            }
        };

        parse(v1, parts1);
        parse(v2, parts2);

        const size_t maxLen = std::max(parts1.size(), parts2.size());
        parts1.resize(maxLen, 0);
        parts2.resize(maxLen, 0);

        for (size_t i = 0; i < maxLen; ++i) {
            if (parts1[i] < parts2[i])
                return -1;
            if (parts1[i] > parts2[i])
                return 1;
        }

        return 0;
    }

    ResolutionResult VersionResolver::resolveDependency(const std::vector<ModuleMetadata> &allModules,
                                                        const DependencyRequirement &dependency,
                                                        const ModuleMetadata &requestingModule) {
        ResolutionResult result;
        result.requestedPackageId = dependency.packageId;
        result.requestedModuleId = dependency.moduleId;
        result.versionRange = dependency.versionRange;
        result.requestedLevel = dependency.level;

        std::vector<ModuleMetadata> candidates;

        for (const auto &module : allModules) {
            if (module.packageId != dependency.packageId || module.moduleId != dependency.moduleId)
                continue;
            if (module.level == -1) {
                std::ostringstream oss;
                oss << "[ERROR] Module " << module.packageId << ":" << module.moduleId << " has invalid level -1"
                    << "\n";
                result.success = false;
                result.error = oss.str();
                return result;
            }
            if (dependency.level != -1 && module.level != dependency.level)
                continue;
            candidates.push_back(module);
        }

        if (candidates.empty()) {
            result.success = false;
            std::ostringstream oss;
            oss << "[ERROR] Dependency not found" << "\n";
            oss << "  Requesting module: " << requestingModule.packageId << "::" << requestingModule.moduleId << " (v"
                << requestingModule.version << ", level " << requestingModule.level << ")" << "\n";
            oss << "  Required: " << dependency.packageId << "::" << dependency.moduleId << " (level "
                << (dependency.level == -1 ? "any" : std::to_string(dependency.level))
                << ", version: " << (dependency.versionRange.empty() ? "any" : dependency.versionRange) << ")"
                << "\n";

            std::vector<std::pair<std::string, std::string>> availableModules;
            for (const auto &module : allModules) {
                if (module.packageId == dependency.packageId) {
                    std::string display = module.moduleId + " (v" + module.version + ", level " +
                                          std::to_string(module.level) + ")";
                    availableModules.emplace_back(module.version, std::move(display));
                }
            }

            if (availableModules.empty()) {
                oss << "  Available modules in package '" << dependency.packageId << "': none" << "\n";
            } else {
                oss << "  Available modules in package '" << dependency.packageId << "':" << "\n";
                std::sort(availableModules.begin(), availableModules.end(),
                          [](const auto &a, const auto &b) {
                              return VersionRange::compareVersions(a.first, b.first) > 0;
                          });
                for (const auto &mod : availableModules)
                    oss << "    - " << mod.second << "\n";
            }

            result.error = oss.str();
            return result;
        }

        std::vector<std::string> allVersions;
        std::map<std::string, int> versionToLevel;

        for (const auto &cand : candidates) {
            allVersions.push_back(cand.version);
            versionToLevel[cand.version] = cand.level;
        }

        std::sort(allVersions.begin(), allVersions.end());
        allVersions.erase(std::unique(allVersions.begin(), allVersions.end()), allVersions.end());
        result.candidates = allVersions;

        std::vector<std::string> versionsInRange;
        if (dependency.versionRange.empty() || dependency.versionRange == "*") {
            versionsInRange = allVersions;
        } else {
            VersionRange range(dependency.versionRange);
            if (!range.valid()) {
                result.success = false;
                std::ostringstream oss;
                oss << "[ERROR] Invalid version range" << "\n";
                oss << "  Requesting module: " << requestingModule.packageId << "::"
                    << requestingModule.moduleId << "\n";
                oss << "  Version range: " << dependency.versionRange << " (error: " << range.parseError()
                    << ")" << "\n";
                result.error = oss.str();
                return result;
            }
            versionsInRange = range.getVersionsInRange(allVersions);
            result.versionRange = range.toString();
        }

        if (versionsInRange.empty()) {
            result.success = false;
            std::ostringstream oss;
            oss << "[ERROR] No version in range" << "\n";
            oss << "  Requesting module: " << requestingModule.packageId << "::" << requestingModule.moduleId << " (v"
                << requestingModule.version << ", level " << requestingModule.level << ")" << "\n";
            oss << "  Required: " << dependency.packageId << "::" << dependency.moduleId << "\n";
            oss << "  Version range: " << dependency.versionRange << "\n";
            oss << "  Available versions: ";
            for (size_t i = 0; i < allVersions.size(); ++i) {
                if (i > 0)
                    oss << ", ";
                oss << allVersions[i];
            }
            oss << "\n";
            result.error = oss.str();
            return result;
        }

        result.versionsInRange = versionsInRange;

        std::vector<std::string> versionsByLevel;
        if (dependency.level != -1) {
            for (const auto &version : versionsInRange) {
                if (versionToLevel[version] == dependency.level)
                    versionsByLevel.push_back(version);
            }

            if (versionsByLevel.empty()) {
                result.success = false;
                std::ostringstream oss;
                oss << "[ERROR] No version with required level" << "\n";
                oss << "  Requesting module: " << requestingModule.packageId << "::" << requestingModule.moduleId
                    << " (v" << requestingModule.version << ", level " << requestingModule.level << ")" << "\n";
                oss << "  Required: " << dependency.packageId << "::" << dependency.moduleId << "\n";
                oss << "  Version range: " << dependency.versionRange << "\n";
                oss << "  Required level: " << dependency.level << "\n";
                oss << "  Versions in range: ";
                for (size_t i = 0; i < versionsInRange.size(); ++i) {
                    if (i > 0)
                        oss << ", ";
                    oss << versionsInRange[i] << " (level " << versionToLevel[versionsInRange[i]] << ")";
                }
                result.error = oss.str();
                return result;
            }
        } else {
            for (const auto &version : versionsInRange) {
                if (versionToLevel[version] == requestingModule.level)
                    versionsByLevel.push_back(version);
            }

            if (versionsByLevel.empty()) {
                result.success = false;
                std::ostringstream oss;
                oss << "[ERROR] No compatible level version" << "\n";
                oss << "  Requesting module: " << requestingModule.packageId << "::" << requestingModule.moduleId
                    << " (v" << requestingModule.version << ", level " << requestingModule.level << ")" << "\n";
                oss << "  Required: " << dependency.packageId << "::" << dependency.moduleId << "\n";
                oss << "  Version range: " << dependency.versionRange << "\n";
                oss << "  Compatible level: " << requestingModule.level << "\n";
                oss << "  Versions in range: ";
                for (size_t i = 0; i < versionsInRange.size(); ++i) {
                    if (i > 0)
                        oss << ", ";
                    oss << versionsInRange[i] << " (level " << versionToLevel[versionsInRange[i]] << ")";
                }
                result.error = oss.str();
                return result;
            }
        }

        std::string bestVersion = selectHighestVersion(versionsByLevel);

        if (bestVersion.empty()) {
            result.success = false;
            std::ostringstream oss;
            oss << "[ERROR] Cannot select best version" << "\n";
            oss << "  Requesting module: " << requestingModule.packageId << "::" << requestingModule.moduleId << " (v"
                << requestingModule.version << ", level " << requestingModule.level << ")" << "\n";
            oss << "  Required: " << dependency.packageId << "::" << dependency.moduleId << "\n";
            oss << "  Candidates: ";
            for (size_t i = 0; i < versionsByLevel.size(); ++i) {
                if (i > 0)
                    oss << ", ";
                oss << versionsByLevel[i];
            }
            result.error = oss.str();
            return result;
        }

        result.success = true;
        result.resolvedVersion = bestVersion;
        result.resolvedLevel = versionToLevel[bestVersion];
        return result;
    }

    std::string VersionResolver::selectHighestVersion(const std::vector<std::string> &versions) {
        if (versions.empty()) {
            // TODO: Re-enable logging when srt::core::Logger is migrated
            // DependencyLog.langCoreWarning("Cannot select highest version from empty list");
            return "";
        }

        std::vector<std::string> sortedVersions = versions;
        std::sort(sortedVersions.begin(), sortedVersions.end(),
                  [](const std::string &a, const std::string &b) { return VersionRange::compareVersions(a, b) > 0; });

        return sortedVersions.front();
    }
} // namespace srt::dependency
