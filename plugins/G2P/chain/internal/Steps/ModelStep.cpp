#include "ModelStep.h"
#include <synthrt/Core/Support/ConfigAccessor.h>
#include <synthrt/G2P/Support/ContextUtils.h>
#include <synthrt/G2P/Support/Error.h>
#include <synthrt/Core/Support/Logging.h>
#include <synthrt/G2P/Core/PackageManager.h>

namespace srt::g2p::plugins::ChainG2p
{
    static srt::LogCategory ModelLog("chainG2p.model");

    srt::core::Expected<void> ModelStep::configure(const srt::g2p::ModuleSpec *spec,
                                                   const srt::core::JsonObject &config)
    {
        m_spec = spec;

        // 解析 enabled
        auto enabledIt = config.find("enabled");
        if (enabledIt != config.end() && enabledIt->second.isBool()) {
            m_enabled = enabledIt->second.toBool();
        } else {
            m_enabled = true;
        }

        if (!m_enabled) {
            return {};
        }

        // 解析 onnxG2pId - 从传入的 config 中读取
        auto idIt = config.find("id");
        if (idIt == config.end() || !idIt->second.isString()) {
            return srt::g2p::Error(srt::g2p::Error::ConfigError, "Missing required field: id");
        }
        m_onnxG2pId = idIt->second.toString();

        // 解析 batchSize
        auto batchSizeIt = config.find("batchSize");
        if (batchSizeIt != config.end() && batchSizeIt->second.isInt()) {
            m_batchSize = static_cast<int>(batchSizeIt->second.toInt());
        } else {
            m_batchSize = kModelStepBatchSize;
        }

        // 获取 G2p 任务（graceful degradation）
        // NOTE: Source used spec->Mgr(); synthrt's ModuleSpec lacks Mgr(), so
        // we use m_task->Mgr() (Task has Mgr() that returns the PackageManager).
        srt::g2p::PackageManager* mgr = m_task ? m_task->Mgr() : nullptr;
        if (!mgr) {
            ModelLog.srtWarning("Model step: PackageManager unavailable, "
                                 "model inference will be disabled for '%1'", m_onnxG2pId);
            m_enabled = false;
            return {};
        }

        auto g2pCate = mgr->category(srt::g2p::kG2pCategory);
        if (!g2pCate) {
            ModelLog.srtWarning("Model step: category 'g2p' not found, "
                                 "model inference will be disabled for '%1'", m_onnxG2pId);
            m_enabled = false;
            return {};
        }

        // FQID 两级查找：本 context 优先，找不到且非默认 context 时回退默认 context（官方兜底）
        const auto ctxKey = spec->contextKey();
        const auto fqid = srt::g2p::ContextUtils::formatFqid(ctxKey, m_onnxG2pId);
        auto g2pObj = g2pCate->getFirstObject(fqid);
        if (!g2pObj && !ctxKey.isDefault()) {
            // 声库 context 找不到 → 回退默认 context（裸 id = 默认 context 的 FQID）
            g2pObj = g2pCate->getFirstObject(m_onnxG2pId);
        }
        if (!g2pObj) {
            ModelLog.srtWarning("Model step: g2p task '%1' not found, "
                                 "model inference will be disabled. Words needing inference will use original lyrics.",
                                 m_onnxG2pId);
            m_enabled = false;
            return {};
        }

        m_onnxTask = g2pObj.as<srt::g2p::Task>();

        return {};
    }

    void ModelStep::handle(G2pContext &context)
    {
        if (!m_enabled || !m_onnxTask) {
            // Mark words that need inference with DriverUnavailable
            if (!m_onnxTask) {
                for (auto &word : context.words()) {
                    if (word.mode == srt::g2p::kG2pModeConvert && !word.discard && !word.fromDict &&
                        word.pronunciation.empty()) {
                        word.pronunciation = word.lyric;
                        word.candidates = {word.lyric};
                        word.errorType = srt::g2p::DriverUnavailable;
                    }
                }
            }
            return;
        }

        // 收集需要模型推理的词
        std::vector<size_t> needInferenceIndices;
        std::vector<std::string> needInferenceWords;

        for (size_t i = 0; i < context.words().size(); ++i) {
            auto &word = context.words()[i];

            // 只处理需要转换、未丢弃、且字典查不到的词
            if (word.mode == srt::g2p::kG2pModeConvert && !word.discard && !word.fromDict &&
                word.pronunciation.empty()) {
                needInferenceIndices.push_back(i);
                // 使用清洗后的词（如果有）
                needInferenceWords.push_back(
                    word.cleanedLyric.empty() ? word.lyric : word.cleanedLyric
                );
            }
        }

        // 批量处理
        for (size_t batchStart = 0; batchStart < needInferenceWords.size(); batchStart += m_batchSize) {
            size_t batchEnd = std::min(batchStart + m_batchSize, needInferenceWords.size());

            std::vector<size_t> batchIndices(
                needInferenceIndices.begin() + batchStart,
                needInferenceIndices.begin() + batchEnd
            );
            std::vector<std::string> batchWords(
                needInferenceWords.begin() + batchStart,
                needInferenceWords.begin() + batchEnd
            );

            processBatch(context, batchIndices, batchWords);
        }
    }

    void ModelStep::processBatch(G2pContext &context,
                                  const std::vector<size_t> &indices,
                                  const std::vector<std::string> &words)
    {
        // 创建批量输入
        auto batchInput = srt::core::NO<srt::g2p::G2pInputV1>::create();
        for (const auto &word : words) {
            batchInput->g2pInput.push_back(word);
        }

        // 调用模型
        auto resultExp = m_onnxTask->start(batchInput);
        if (!resultExp) {
            // 批量转换失败，所有词使用原词
            for (size_t idx : indices) {
                auto &word = context.words()[idx];
                word.pronunciation = word.lyric;
                word.candidates = {word.lyric};
                word.errorType = srt::g2p::ModelInferenceFailed;
            }
            return;
        }

        auto result = resultExp.take();
        if (const auto g2pResult = result.as<srt::g2p::G2pResultV1>()) {
            // 将结果映射回原始位置
            for (size_t i = 0; i < indices.size(); ++i) {
                size_t originalIndex = indices[i];
                if (i < g2pResult->g2pResult.size()) {
                    auto &word = context.words()[originalIndex];
                    const auto &resultWord = g2pResult->g2pResult[i];

                    word.pronunciation = resultWord.pronunciation;
                    word.candidates = resultWord.candidates;
                    word.fromModel = true;
                    word.errorType = resultWord.errorType;
                } else {
                    // 结果数量不匹配
                    auto &word = context.words()[originalIndex];
                    word.pronunciation = word.lyric;
                    word.candidates = {word.lyric};
                    word.errorType = srt::g2p::ModelInferenceFailed;
                }
            }
        } else {
            // 返回结果类型错误
            for (size_t idx : indices) {
                auto &word = context.words()[idx];
                word.pronunciation = word.lyric;
                word.candidates = {word.lyric};
                word.errorType = srt::g2p::ModelInferenceFailed;
            }
        }
    }

    void ModelStep::cleanup()
    {
        m_onnxTask.reset();
    }

} // namespace srt::g2p::plugins::ChainG2p
