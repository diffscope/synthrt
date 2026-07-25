#include <synthrt/Core/Dependency/DependencyResolver.h>

#include <algorithm>
#include <iostream>
#include <sstream>
#include <unordered_set>

// TODO: Re-enable logging when srt::core::Logger is migrated (see docs/refactoring-v3/07-engineering-conventions.md §6)
// #include <LangCore/Core/ManagerLogger.h>
#include <synthrt/Core/Dependency/DependencyGraph.h>
#include <synthrt/Core/Dependency/VersionUtils.h>

namespace srt::dependency {

    bool DependencyResolver::resolveAllDependencies(std::vector<ModuleMetadata> &modules,
                                                    const std::vector<ModuleMetadata> &fallbackModules) {
        clear();

        modules.erase(std::remove_if(modules.begin(), modules.end(),
                                     [this](const ModuleMetadata &module)
                                     {
                                         for (const auto &depRaw : module.requirements) {
                                             if (depRaw.packageId == module.packageId &&
                                                 depRaw.moduleId == module.moduleId && depRaw.level == module.level) {
                                                 std::ostringstream oss;
                                                 oss << "[ERROR] Removed module " << module.packageId
                                                     << "::" << module.moduleId << ": level " << module.level
                                                     << " because it depends on itself!" << std::endl;
                                                  errors_.push_back(oss.str());
                                                  return true;
                                             }
                                         }
                                         return false;
                                     }),
                      modules.end());

        selectBestModules(modules);
        buildIndex(modules);

        std::unordered_map<std::string, bool> resolvedStatus;

        for (auto &module : modules) {
            resolvedStatus[module.key()] = module.resolvedDependencies.size() == module.requirements.size();
        }

        size_t maxIterations = modules.size() * 2;
        for (size_t iteration = 0; iteration < maxIterations; ++iteration) {
            bool progressMade = false;

            for (auto &module : modules) {
                std::string moduleKey = module.key();

                if (resolvedStatus[moduleKey]) {
                    continue;
                }

                bool canResolve = true;
                std::vector<ResolvedDependency> newResolvedDeps;

                for (const auto &depRaw : module.requirements) {
                    bool alreadyResolved = false;
                    for (const auto &existingDep : module.resolvedDependencies) {
                        if (existingDep.packageId == depRaw.packageId && existingDep.moduleId == depRaw.moduleId && existingDep.level == depRaw.level) {
                            alreadyResolved = true;
                            newResolvedDeps.push_back(existingDep);
                            break;
                        }
                    }

                    if (alreadyResolved) {
                        continue;
                    }

                    std::vector<ModuleMetadata> candidates;
                    for (const auto &candidate : modules) {
                        if (candidate.packageId == depRaw.packageId && candidate.moduleId == depRaw.moduleId) {
                            candidates.push_back(candidate);
                        }
                    }

                    if (candidates.empty()) {
                        if (!fallbackModules.empty()) {
                            // Search fallback (default context) modules
                            for (const auto &candidate : fallbackModules) {
                                if (candidate.packageId == depRaw.packageId &&
                                    candidate.moduleId == depRaw.moduleId) {
                                    candidates.push_back(candidate);
                                }
                            }

                            if (!candidates.empty()) {
                                // TODO: Re-enable logging when srt::core::Logger is migrated
                                // DependencyLog.langCoreDebug(
                                //     "Dependency resolved via default context fallback: %1::%2 for module %3::%4",
                                //     depRaw.packageId, depRaw.moduleId, module.packageId, module.moduleId);
                            } else {
                                // Check if dependency exists in a different known context (cross-context)
                                // fallbackModules are from default context; modules are from current context.
                                // If not found in either, emit Dep-1 as usual.
                                std::ostringstream oss;
                                oss << "[ERROR] Direct dependency missing: " << depRaw.packageId
                                    << "::" << depRaw.moduleId
                                    << " required by module: " << module.packageId << "::" << module.moduleId
                                    << std::endl;
                                oss << "  Requesting module: " << module.packageId << "::" << module.moduleId
                                    << " (v" << module.version << ", level " << module.level << ")" << std::endl;
                                oss << "  Required: " << depRaw.packageId << "::" << depRaw.moduleId << " (level "
                                    << (depRaw.level == -1 ? "any" : std::to_string(depRaw.level))
                                    << ", version: "
                                    << (depRaw.versionRange.empty() ? "any" : depRaw.versionRange) << ")"
                                    << std::endl;

                                errors_.push_back(oss.str());
                                canResolve = false;
                                break;
                            }
                        } else {
                            std::ostringstream oss;
                            oss << "[ERROR] Direct dependency missing: " << depRaw.packageId
                                << "::" << depRaw.moduleId
                                << " required by module: " << module.packageId << "::" << module.moduleId
                                << std::endl;
                            oss << "  Requesting module: " << module.packageId << "::" << module.moduleId << " (v"
                                << module.version << ", level " << module.level << ")" << std::endl;
                            oss << "  Required: " << depRaw.packageId << "::" << depRaw.moduleId << " (level "
                                << (depRaw.level == -1 ? "any" : std::to_string(depRaw.level))
                                << ", version: " << (depRaw.versionRange.empty() ? "any" : depRaw.versionRange)
                                << ")" << std::endl;

                            errors_.push_back(oss.str());
                            canResolve = false;
                            break;
                        }
                    }

                    auto result = VersionResolver::resolveDependency(candidates, depRaw, module);

                    if (!result.success) {
                        errors_.push_back(result.error);
                        canResolve = false;
                        break;
                    }

                    ResolvedDependency resolvedDep;
                    resolvedDep.packageId = depRaw.packageId;
                    resolvedDep.moduleId = depRaw.moduleId;
                    resolvedDep.level = result.resolvedLevel;
                    resolvedDep.version = result.resolvedVersion;
                    newResolvedDeps.push_back(resolvedDep);
                }

                if (canResolve) {
                    module.resolvedDependencies = newResolvedDeps;
                    resolvedStatus[moduleKey] = true;
                    progressMade = true;
                }
            }

            if (!progressMade) {
                std::vector<std::string> unresolvedModules;
                for (const auto &module : modules) {
                    if (!resolvedStatus[module.key()]) {
                        unresolvedModules.push_back(module.packageId + "::" + module.moduleId);
                    }
                }

                if (!unresolvedModules.empty()) {
                    std::ostringstream oss;
                    oss << "[ERROR] Cannot resolve dependencies for " << unresolvedModules.size()
                        << " module(s):" << std::endl;
                    for (const auto &modName : unresolvedModules) {
                        oss << "  - " << modName << std::endl;
                    }
                    oss << "Possible circular dependencies or missing dependencies." << std::endl;
                    errors_.push_back(oss.str());
                }
                break;
            }

            bool allDone = true;
            for (const auto &module : modules) {
                if (!resolvedStatus[module.key()]) {
                    allDone = false;
                    break;
                }
            }

            if (allDone) {
                break;
            }
        }

        for (const auto &module : modules) {
            if (resolvedStatus[module.key()]) {
                resolvedModules_.push_back(module);
            }
        }

        if (!errors_.empty()) {
            return false;
        }

        return true;
    }

    void DependencyResolver::buildIndex(const std::vector<ModuleMetadata> &modules) {
        moduleIndex_.clear();
        for (const auto &module : modules) {
            std::string key = module.packageId + "::" + module.moduleId + "::" + std::to_string(module.level);
            moduleIndex_[key].push_back(const_cast<ModuleMetadata *>(&module));
        }
    }

    void DependencyResolver::selectBestModules(std::vector<ModuleMetadata> &modules) {
        // §14.18 fix: use index-based lookup instead of raw pointers to avoid
        // iterator/pointer invalidation when remove_if moves elements.
        std::unordered_map<std::string, size_t> bestIndexes; // uniqueKey -> index of best module

        for (size_t i = 0; i < modules.size(); ++i) {
            const auto &module = modules[i];
            // CODING-05: use ModuleMetadata::uniqueKey() which includes iid +
            // type (the manual拼接 below missed them, causing modules of
            // different iid/type to be erroneously de-duplicated).
            std::string uniqueKey = module.uniqueKey();

            if (auto it = bestIndexes.find(uniqueKey); it == bestIndexes.end()) {
                bestIndexes[uniqueKey] = i;
            } else {
                if (VersionRange::compareVersions(module.version, modules[it->second].version) > 0) {
                    it->second = i;
                }
            }
        }

        // Collect the uniqueKeys of selected modules (identified by their content, not address)
        std::unordered_set<std::string> selectedKeys;
        for (const auto &[uniqueKey, idx] : bestIndexes) {
            selectedKeys.insert(modules[idx].key());
        }

        modules.erase(std::remove_if(modules.begin(), modules.end(),
                                     [&selectedKeys](const ModuleMetadata &module)
                                     {
                                         return selectedKeys.find(module.key()) == selectedKeys.end();
                                     }),
                      modules.end());
    }

    void DependencyResolver::clear() {
        resolvedModules_.clear();
        errors_.clear();
        moduleIndex_.clear();
    }
} // namespace srt::dependency
