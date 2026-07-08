#include <synthrt/G2P/Core/Manager.h>

#include <algorithm>
#include <shared_mutex>

#include <stdcorelib/path.h>
#include <stdcorelib/pimpl.h>

#include <synthrt/Core/Dependency/DependencyGraph.h>
#include <synthrt/Core/Dependency/LevelCompatibilityChecker.h>
#include <synthrt/Core/Support/Logging.h>
#include <synthrt/G2P/Support/ContextUtils.h>
#include <synthrt/G2P/Support/Error.h>
#include <synthrt/G2P/Task/G2pTask.h>
#include <synthrt/G2P/Task/Task.h>

#include "Core/PackageManager_p.h"

namespace fs = std::filesystem;

namespace srt::g2p {

    namespace {
        srt::core::LogCategory g2pLog("g2p");

        std::string g2pSourceFromContext(const std::string &g2pContext) {
            return g2pContext.empty() ? kG2pSourceOfficial : kG2pSourceVoicebank;
        }
    }

    Manager::Manager() : PackageManager(*new PackageManager::Impl(this)) {}

    Manager::~Manager() = default;

    Manager *Manager::instance() {
        static Manager inst;
        return &inst;
    }

    srt::core::Expected<void> Manager::loadTasksForCategory(const std::string &category) {
        auto &impl = *static_cast<PackageManager::Impl *>(_impl.get());
        std::unique_lock<std::shared_mutex> lock(impl.tasks_mtx);

        for (const auto &[ctxKey, state] : impl.contextStates) {
            if (state != ContextState::Ready)
                continue;

            auto &ctxModuleInfos = impl.contextModuleInfos[ctxKey];
            for (const auto &moduleInfo : ctxModuleInfos) {
                if (moduleInfo.type != category)
                    continue;

                const auto fqid = ContextUtils::formatFqid(ctxKey, moduleInfo.moduleId);
                const auto *cate = this->category(category);
                if (!cate)
                    continue;

                const auto obj = cate->getFirstObject(fqid);
                if (!obj)
                    continue;

                impl.tasks[category][ctxKey][moduleInfo.moduleId] = obj.as<Task>();
            }
        }
        return {};
    }

    static bool resolveLevelCompatibility(const srt::dependency::ModuleMetadata &info) {
        srt::dependency::LevelCompatibilityChecker::LevelConfig levelConfig;
        levelConfig.currentLevel = 2;
        levelConfig.minimumLevel = 1;
        levelConfig.maximumLevel = 2;
        auto checkResult = srt::dependency::LevelCompatibilityChecker::checkCorePlugin(info.level, levelConfig);
        return checkResult.isCompatible;
    }

    static std::vector<srt::dependency::ModuleMetadata> filterCompatibleModules(
        const std::vector<srt::dependency::ModuleMetadata> &moduleInfos) {
        std::vector<srt::dependency::ModuleMetadata> compatible;
        for (const auto &info : moduleInfos) {
            if (resolveLevelCompatibility(info))
                compatible.push_back(info);
        }
        return compatible;
    }

    srt::core::Expected<void> Manager::initialize() {
        auto &impl = *static_cast<PackageManager::Impl *>(_impl.get());

        if (impl.initialized) {
            return Error(Error::AlreadyInitialized,
                         "Manager::initialize() has already been called");
        }

        // Phase 1: Default context
        {
            srt::core::ContextKey defaultCtx("");
            const auto moduleInfos = this->getModuleMetadatas(defaultCtx);
            if (moduleInfos.empty()) {
                return Error(Error::InitializationError,
                             "Ord-1: Default context initialization failed: no modules found");
            }

            auto compatibleModules = filterCompatibleModules(moduleInfos);
            if (compatibleModules.empty()) {
                return Error(Error::InitializationError,
                             "Ord-1: Default context has no compatible modules after Level check");
            }

            auto &depGraph = impl.contextDependencyGraphs[srt::core::ContextKey("")];
            depGraph.clear();
            for (const auto &info : compatibleModules)
                depGraph.addModule(info);

            if (!depGraph.buildGraph()) {
                return Error(Error::DependencyError,
                             "Ord-1: Failed to build dependency graph for default context");
            }

            if (const auto cycles = depGraph.findCycles(); !cycles.empty()) {
                return Error(Error::DependencyError,
                             "Ord-1: Circular dependencies detected in default context");
            }

            auto packageOrder = depGraph.getPackageInitializationOrder();
            int failedPkgCount = 0;
            std::vector<std::string> pkgErrors;
            for (const auto &packageInfo : packageOrder) {
                auto exp = this->open(packageInfo.packagePath);
                if (!exp) {
                    pkgErrors.push_back(packageInfo.packageId + ": " + exp.error().message());
                    failedPkgCount++;
                    continue;
                }

                Package pkg = exp.take();
                if (!pkg.isLoaded()) {
                    pkgErrors.push_back(packageInfo.packageId + ": " + pkg.error().message());
                    failedPkgCount++;
                    continue;
                }

                for (const auto &moduleInfo : packageInfo.initializationOrder) {
                    auto taskExp = createModuleTask(moduleInfo, pkg);
                    if (taskExp) {
                        const auto fqid = ContextUtils::formatFqid(defaultCtx, moduleInfo.moduleId);
                        if (auto *cate = this->category(moduleInfo.type)) {
                            cate->addObject(fqid, taskExp.value());
                        }
                    } else {
                        g2pLog.srtWarning("G2P task init failed: package=%1, module=%2, type=%3 - %4",
                                          packageInfo.packageId, moduleInfo.moduleId,
                                          moduleInfo.type, taskExp.error().message());
                    }
                }
            }

            if (failedPkgCount > 0 && packageOrder.size() == static_cast<size_t>(failedPkgCount)) {
                std::string detail = "Ord-1: All packages failed to load in default context";
                for (const auto &e : pkgErrors) {
                    detail += "\n  " + e;
                }
                return Error(Error::InitializationError, detail);
            }

            impl.contextStates[defaultCtx] = ContextState::Ready;
        }

        // Phase 2: Non-default contexts
        for (const auto &[ctxKey, _] : impl.contextPackagePaths) {
            if (ctxKey.isDefault())
                continue;

            const auto moduleInfos = this->getModuleMetadatas(ctxKey);
            if (moduleInfos.empty()) {
                impl.contextStates[ctxKey] = ContextState::Failed;
                continue;
            }

            auto compatibleModules = filterCompatibleModules(moduleInfos);
            if (compatibleModules.empty()) {
                impl.contextStates[ctxKey] = ContextState::Failed;
                continue;
            }

            auto &depGraph = impl.contextDependencyGraphs[ctxKey];
            depGraph.clear();
            for (const auto &info : compatibleModules)
                depGraph.addModule(info);

            if (!depGraph.buildGraph()) {
                impl.contextStates[ctxKey] = ContextState::Failed;
                continue;
            }

            if (const auto cycles = depGraph.findCycles(); !cycles.empty()) {
                impl.contextStates[ctxKey] = ContextState::Failed;
                continue;
            }

            auto packageOrder = depGraph.getPackageInitializationOrder();
            bool allFailed = true;
            for (const auto &packageInfo : packageOrder) {
                auto exp = this->open(packageInfo.packagePath);
                if (!exp)
                    continue;

                Package pkg = exp.take();
                if (!pkg.isLoaded())
                    continue;

                allFailed = false;
                for (const auto &moduleInfo : packageInfo.initializationOrder) {
                    auto taskExp = createModuleTask(moduleInfo, pkg);
                    if (taskExp) {
                        const auto fqid = ContextUtils::formatFqid(ctxKey, moduleInfo.moduleId);
                        if (auto *cate = this->category(moduleInfo.type)) {
                            cate->addObject(fqid, taskExp.value());
                        }
                    } else {
                        g2pLog.srtWarning("G2P task init failed: package=%1, module=%2, type=%3 - %4",
                                          packageInfo.packageId, moduleInfo.moduleId,
                                          moduleInfo.type, taskExp.error().message());
                    }
                }
            }

            if (allFailed && !packageOrder.empty()) {
                impl.contextStates[ctxKey] = ContextState::Failed;
            } else {
                impl.contextStates[ctxKey] = ContextState::Ready;
            }
        }

        // Phase 3: Load tasks for categories
        if (auto result = loadTasksForCategory(kG2pCategory); !result) {
            return Error(Error::RuntimeError,
                         "Failed to load g2p tasks: " + result.error().message());
        }

        if (auto result = loadTasksForCategory(kDictCategory); !result) {
            // dict is optional - silently continue
        }

        impl.initialized = true;
        return {};
    }

    bool Manager::initialized() const {
        return static_cast<PackageManager::Impl *>(_impl.get())->initialized;
    }

    srt::core::Expected<srt::core::NO<Task>> Manager::task(
        const std::string &category, const std::string &context,
        const std::string &id) const {
        return task(category, context, {}, id);
    }

    srt::core::Expected<srt::core::NO<Task>> Manager::task(
        const std::string &category, const std::string &context,
        const stdc::VersionNumber &version, const std::string &id) const {
        if (category.empty())
            return Error(Error::ValidationError, "T-1: category cannot be empty");

        if (auto exp = ContextUtils::validateContextName(context); !exp)
            return Error(Error::ValidationError, "T-2: " + exp.error().message());

        if (id.empty())
            return Error(Error::ValidationError, "T-3: id cannot be empty");

        if (auto exp = ContextUtils::validateModuleId(id); !exp)
            return Error(Error::ValidationError, "T-4: " + exp.error().message());

        auto &impl = *static_cast<PackageManager::Impl *>(_impl.get());
        std::shared_lock<std::shared_mutex> lock(impl.tasks_mtx);

        auto catIt = impl.tasks.find(category);
        if (catIt == impl.tasks.end())
            return Error(Error::RuntimeError, "T-5: could not find category: " + category);

        srt::core::ContextKey ctxKey(context, version);
        auto ctxIt = catIt->second.find(ctxKey);
        if (ctxIt == catIt->second.end()) {
            auto stateIt = impl.contextStates.find(ctxKey);
            if (stateIt != impl.contextStates.end() && stateIt->second == ContextState::Failed)
                return Error(Error::RuntimeError,
                             "T-6: context '" + ctxKey.toString() + "' failed initialization");
            return Error(Error::RuntimeError,
                         "T-6: could not find context: " + ctxKey.toString());
        }

        auto idIt = ctxIt->second.find(id);
        if (idIt == ctxIt->second.end())
            return Error(Error::RuntimeError,
                         "T-7: could not find id: " + id + " in context " + ctxKey.toString());

        return idIt->second;
    }

    srt::core::Expected<std::vector<srt::core::NO<Task>>> Manager::tasks(
        const std::string &category, const std::string &context) const {
        return tasks(category, context, {});
    }

    srt::core::Expected<std::vector<srt::core::NO<Task>>> Manager::tasks(
        const std::string &category, const std::string &context,
        const stdc::VersionNumber &version) const {
        if (category.empty())
            return Error(Error::ValidationError, "category cannot be empty");

        if (auto exp = ContextUtils::validateContextName(context); !exp)
            return exp.error();

        auto &impl = *static_cast<PackageManager::Impl *>(_impl.get());
        std::shared_lock<std::shared_mutex> lock(impl.tasks_mtx);

        auto catIt = impl.tasks.find(category);
        if (catIt == impl.tasks.end())
            return Error(Error::RuntimeError, "could not find category: " + category);

        srt::core::ContextKey ctxKey(context, version);
        auto ctxIt = catIt->second.find(ctxKey);
        if (ctxIt == catIt->second.end())
            return Error(Error::RuntimeError, "could not find context: " + ctxKey.toString());

        std::vector<srt::core::NO<Task>> result;
        result.reserve(ctxIt->second.size());
        for (const auto &[_, t] : ctxIt->second)
            result.push_back(t);

        if (result.empty())
            return Error(Error::RuntimeError,
                         "category: " + category + " is empty in context " + ctxKey.toString());

        return result;
    }

    std::vector<G2pRes> Manager::convert(const std::vector<G2pInput> &input) {
        if (input.empty())
            return {};

        auto &impl = *static_cast<PackageManager::Impl *>(_impl.get());

        if (!impl.initialized) {
            std::vector<G2pRes> results;
            results.reserve(input.size());
            for (const auto &item : input) {
                results.emplace_back(item.lyric, item.g2pId, item.g2pContext,
                                     item.g2pContextVersion, item.lyric,
                                     std::vector<std::string>{item.lyric}, kG2pModeCopy,
                                     NotInitialized, g2pSourceFromContext(item.g2pContext));
            }
            return results;
        }

        std::vector<G2pRes> result;
        result.reserve(input.size());

        struct Group {
            std::string g2pContext;
            stdc::VersionNumber g2pContextVersion;
            std::string g2pId;
            std::vector<std::string> lyrics;
            std::vector<size_t> resultIndexes;
        };

        std::vector<Group> groups;
        result.resize(input.size());

        for (size_t i = 0; i < input.size(); ++i) {
            const auto &item = input[i];

            if (item.lyric.empty()) {
                result[i] = G2pRes("", item.g2pId, item.g2pContext, item.g2pContextVersion,
                                   "", {}, kG2pModeSkip, NoError,
                                   g2pSourceFromContext(item.g2pContext));
                continue;
            }

            if (item.g2pId.empty()) {
                result[i] = G2pRes(item.lyric, "", item.g2pContext, item.g2pContextVersion,
                                   item.lyric, {item.lyric}, kG2pModeCopy, InvalidLyric,
                                   g2pSourceFromContext(item.g2pContext));
                continue;
            }

            if (auto exp = ContextUtils::validateContextName(item.g2pContext); !exp) {
                result[i] = G2pRes(item.lyric, item.g2pId, item.g2pContext,
                                   item.g2pContextVersion, item.lyric, {item.lyric},
                                   kG2pModeCopy, UnknownError,
                                   g2pSourceFromContext(item.g2pContext));
                continue;
            }

            if (groups.empty() ||
                groups.back().g2pContext != item.g2pContext ||
                groups.back().g2pContextVersion != item.g2pContextVersion ||
                groups.back().g2pId != item.g2pId) {
                groups.push_back({item.g2pContext, item.g2pContextVersion, item.g2pId, {}, {}});
            }
            groups.back().lyrics.push_back(item.lyric);
            groups.back().resultIndexes.push_back(i);
        }

        const auto _input = srt::core::NO<G2pInputV1>::create();

        for (const auto &group : groups) {
            srt::core::NO<Task> taskObj;
            {
                std::shared_lock<std::shared_mutex> lock(impl.tasks_mtx);
                auto catIt = impl.tasks.find(kG2pCategory);
                if (catIt != impl.tasks.end()) {
                    srt::core::ContextKey ctxKey(group.g2pContext, group.g2pContextVersion);
                    auto ctxIt = catIt->second.find(ctxKey);
                    if (ctxIt != catIt->second.end()) {
                        auto idIt = ctxIt->second.find(group.g2pId);
                        if (idIt != ctxIt->second.end())
                            taskObj = idIt->second;
                    }
                }
            }

            if (!taskObj) {
                const auto src = g2pSourceFromContext(group.g2pContext);
                g2pLog.srtWarning("G2P task not found: g2pId=%1, context=%2 - falling back to copy mode",
                                  group.g2pId, group.g2pContext);
                for (size_t j = 0; j < group.lyrics.size(); ++j) {
                    result[group.resultIndexes[j]] =
                        G2pRes(group.lyrics[j], group.g2pId, group.g2pContext,
                               group.g2pContextVersion, group.lyrics[j], {group.lyrics[j]},
                               kG2pModeCopy, UnknownError, src);
                }
                continue;
            }

            _input->g2pInput = group.lyrics;
            auto resultExp = taskObj->start(_input);
            if (!resultExp) {
                const auto src = g2pSourceFromContext(group.g2pContext);
                g2pLog.srtWarning("G2P inference failed: g2pId=%1, context=%2 - %3",
                                  group.g2pId, group.g2pContext, resultExp.error().message());
                for (size_t j = 0; j < group.lyrics.size(); ++j) {
                    result[group.resultIndexes[j]] =
                        G2pRes(group.lyrics[j], group.g2pId, group.g2pContext,
                               group.g2pContextVersion, group.lyrics[j], {group.lyrics[j]},
                               kG2pModeCopy, ModelInferenceFailed, src);
                }
                continue;
            }

            const auto _result = resultExp.take();
            if (const auto g2pRes = _result.as<G2pResultV1>()) {
                const auto src = g2pSourceFromContext(group.g2pContext);
                const auto provided = std::min(g2pRes->g2pResult.size(), group.resultIndexes.size());
                for (size_t j = 0; j < provided; ++j) {
                    auto res = g2pRes->g2pResult[j];
                    res.g2pContext = group.g2pContext;
                    res.g2pContextVersion = group.g2pContextVersion;
                    res.g2pSource = src;
                    result[group.resultIndexes[j]] = std::move(res);
                }
                for (size_t j = provided; j < group.resultIndexes.size(); ++j) {
                    result[group.resultIndexes[j]] =
                        G2pRes(group.lyrics[j], group.g2pId, group.g2pContext,
                               group.g2pContextVersion, group.lyrics[j], {group.lyrics[j]},
                               kG2pModeCopy, ModelInferenceFailed, src);
                }
            } else {
                const auto src = g2pSourceFromContext(group.g2pContext);
                for (size_t j = 0; j < group.lyrics.size(); ++j) {
                    result[group.resultIndexes[j]] =
                        G2pRes(group.lyrics[j], group.g2pId, group.g2pContext,
                               group.g2pContextVersion, group.lyrics[j], {group.lyrics[j]},
                               kG2pModeCopy, UnknownError, src);
                }
            }
        }

        return result;
    }

} // namespace srt::g2p
