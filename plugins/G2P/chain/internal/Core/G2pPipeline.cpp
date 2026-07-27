#include "G2pPipeline.h"
#include <synthrt/G2P/Support/Error.h>
#include <stdcorelib/str.h>

namespace srt::g2p::plugins::ChainG2p {
    /// 格式化步骤错误消息
    /// @param stepIndex 步骤索引
    /// @param stepType 步骤类型
    /// @param message 错误消息
    /// @param suggestion 建议（可选）
    /// @return 格式化后的错误
    static srt::g2p::Error formatStepError(size_t stepIndex, const std::string &stepType,
                                          const std::string &message, const std::string &suggestion = "") {
        std::string formattedMessage = stdc::formatN("Step #%1 (%2): %3", stepIndex, stepType, message);
        return srt::g2p::Error(srt::g2p::ErrorCode::G2pConfigError, formattedMessage, suggestion);
    }

    /// 配置步骤
    /// @param step 步骤对象
    /// @param spec 模块规范
    /// @param params 配置参数
    /// @param stepIndex 步骤索引
    /// @param stepType 步骤类型
    /// @param useDefault 是否使用默认配置
    /// @return 配置结果
    static srt::core::Expected<void> configureStep(std::shared_ptr<G2pStep> step,
                                                  const srt::g2p::ModuleSpec *spec,
                                                  const srt::core::JsonObject &params,
                                                  size_t stepIndex, const std::string &stepType,
                                                  bool useDefault = false) {
        auto configExp = step->configure(spec, params);
        if (!configExp) {
            std::string suggestion = useDefault ?
                "Check the step's default configuration" :
                "Check the 'params' field in your configuration";
            return formatStepError(stepIndex, stepType, "Configuration failed: " + configExp.error().message(), suggestion);
        }
        return {};
    }
    srt::core::Expected<void> G2pPipeline::configure(const srt::core::JsonObject &config)
    {
        // 检查是否是责任链格式（有 steps 字段）
        auto stepsIt = config.find("steps");
        if (stepsIt != config.end() && stepsIt->second.isArray()) {
            // 责任链格式
            const auto &stepsArray = stepsIt->second.toArray();

            // 安全性检查：限制步骤数量 (T7: centralized constant)
            static constexpr size_t kG2pPipelineMaxSteps = 50;
            if (stepsArray.size() > kG2pPipelineMaxSteps) {
                return srt::g2p::Error(srt::g2p::ErrorCode::G2pConfigError,
                                     stdc::formatN("Too many steps: %1 (maximum allowed: %2)", stepsArray.size(), kG2pPipelineMaxSteps),
                                     "Reduce the number of steps in your configuration");
            }

            m_steps.reserve(stepsArray.size());

            for (size_t stepIndex = 0; stepIndex < stepsArray.size(); ++stepIndex) {
                const auto &stepItem = stepsArray[stepIndex];
                if (!stepItem.isObject()) {
                    return srt::g2p::Error(srt::g2p::ErrorCode::G2pConfigError,
                                         stdc::formatN("Step #%1 must be an object", stepIndex));
                }

                const auto &stepObj = stepItem.toObject();

                // 获取步骤类型
                auto stepTypeIt = stepObj.find("step");
                if (stepTypeIt == stepObj.end() || !stepTypeIt->second.isString()) {
                    return srt::g2p::Error(srt::g2p::ErrorCode::G2pConfigError,
                                         stdc::formatN("Step #%1: Missing required field: step", stepIndex),
                                         "Add the 'step' field to specify the step type");
                }
                auto stepType = stepTypeIt->second.toString();

                // 检查是否禁用
                bool enabled = true;
                auto enabledIt = stepObj.find("enabled");
                if (enabledIt != stepObj.end() && enabledIt->second.isBool()) {
                    enabled = enabledIt->second.toBool();
                }
                if (!enabled) {
                    continue;
                }

                // 创建步骤
                auto stepExp = G2pStepFactory::create(stepType);
                if (!stepExp) {
                    // Propagate the original ErrorCode instead of casting through
                    // the deprecated Error::Type enum. The base Error already
                    // carries an ErrorCode in its Diagnostic; reuse it directly
                    // to avoid lossy int→enum→int round-trips.
                    const auto &stepErr = stepExp.error();
                    return srt::g2p::Error(stepErr.code(),
                                           stdc::formatN("Step #%1: Failed to create step type '%2': %3", stepIndex, stepType, stepErr.message()),
                                           stdc::formatN("Check if step type '%1' is supported. Supported types: %2", stepType,
                                                         G2pStepFactory::supportedTypesAsString()));
                }
                auto step = stepExp.take();

                // 设置 Task
                step->setTask(m_task);

                // 配置步骤
                auto paramsIt = stepObj.find("params");
                if (paramsIt != stepObj.end() && paramsIt->second.isObject()) {
                    auto configResult = configureStep(step, m_spec, paramsIt->second.toObject(), stepIndex, stepType, false);
                    if (!configResult) {
                        return configResult.takeError();
                    }
                } else {
                    // 没有 params，使用默认配置
                    auto configResult = configureStep(step, m_spec, srt::core::JsonObject(), stepIndex, stepType, true);
                    if (!configResult) {
                        return configResult.takeError();
                    }
                }

                // 添加到管道
                m_steps.push_back(step);
            }
        } else {
            return srt::g2p::Error(srt::g2p::ErrorCode::G2pConfigError,
                                 "Missing required 'steps' array in configuration",
                                 "Configuration must contain a 'steps' array defining the G2p pipeline");
        }

        return {};
    }

    void G2pPipeline::process(G2pContext &context)
    {
        for (auto &step : m_steps) {
            step->handle(context);
            if (context.isStopProcessing()) {
                break;
            }
        }
    }

    void G2pPipeline::cleanup()
    {
        for (auto &step : m_steps) {
            if (step) {
                step->cleanup();
            }
        }
        m_steps.clear();
    }

}
