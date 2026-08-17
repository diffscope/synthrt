#include "ModelStep.h"

#include <synthrt/Core/Support/Logging.h>
#include <synthrt/G2P/Base/LangCommon.h>
#include <synthrt/G2P/Core/PackageManager.h>
#include <synthrt/G2P/Support/ContextUtils.h>
#include <synthrt/G2P/Support/Error.h>

#include <algorithm>
#include <string>

namespace srt::g2p::plugins::ChainG2p {
    static srt::LogCategory ModelLog("chainG2p.model");

    srt::core::Expected<void> ModelStep::configure(const srt::g2p::ModuleSpec  *spec,
                                                   const srt::core::JsonObject &config) {
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
            return srt::g2p::Error(srt::g2p::ErrorCode::G2pConfigError, "Missing required field: id");
        }
        m_onnxG2pId = idIt->second.toString();

        // 解析 batchSize
        auto batchSizeIt = config.find("batchSize");
        if (batchSizeIt != config.end() && batchSizeIt->second.isInt()) {
            m_batchSize = static_cast<int>(batchSizeIt->second.toInt());
        } else {
            m_batchSize = kModelStepBatchSize;
        }

        // 解析 langRef (optional) - 传递给模型的语言引用，如 "deu/default"
        auto langRefIt = config.find("langRef");
        if (langRefIt != config.end() && langRefIt->second.isString()) {
            m_langRef = langRefIt->second.toString();
        } else {
            m_langRef.clear();
        }

        // 模型任务（如 g2p-multig2p-multi-official）在 handle() 时按需解析
        // （resolveTask），不在 configure 阶段枚举，避免依赖插件尚未注册时
        // 误报警告/误禁用。参见 resolveTask。
        return {};
    }

    srt::core::NO<srt::g2p::Task> ModelStep::resolveTask() const {
        srt::g2p::PackageManager *mgr = m_task ? m_task->Mgr() : nullptr;
        if (!mgr) {
            return nullptr;
        }

        auto g2pCate = mgr->category(srt::g2p::kG2pCategory);
        if (!g2pCate) {
            return nullptr;
        }

        // FQID 两级查找：本 context 优先，找不到且非默认 context 时回退默认 context（官方兜底）
        const auto ctxKey = m_spec ? m_spec->contextKey() : srt::core::ContextKey();
        const auto fqid   = srt::g2p::ContextUtils::formatFqid(ctxKey, m_onnxG2pId);
        auto       g2pObj = g2pCate->getFirstObject(fqid);
        if (!g2pObj && !ctxKey.isDefault()) {
            // 声库 context 找不到 → 回退默认 context（裸 id = 默认 context 的 FQID）
            g2pObj = g2pCate->getFirstObject(m_onnxG2pId);
        }
        if (!g2pObj) {
            return nullptr;
        }

        return g2pObj.as<srt::g2p::Task>();
    }

    /// 去除首尾空白
    static std::string trimWhitespace(const std::string &s) {
        size_t b = s.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) {
            return "";
        }
        size_t e = s.find_last_not_of(" \t\r\n");
        return s.substr(b, e - b + 1);
    }

    void ModelStep::handle(G2pContext &context) {
        if (!m_enabled) {
            return;
        }

        // 模型任务按需解析：依赖插件在此刻应已注册（初始化阶段已解决依赖）；
        // 若仍缺失才是真实故障，此时仅告警一次并回退原词。
        auto onnxTask = resolveTask();
        if (!onnxTask) {
            if (!m_taskWarned.exchange(true)) {
                ModelLog.srtWarning("Model step: g2p task '%1' not found, "
                                    "model inference will be disabled. "
                                    "Words needing inference will use original lyrics.",
                                    m_onnxG2pId);
            }
            for (auto &word : context.words()) {
                if (word.mode == srt::g2p::kG2pModeConvert && !word.discard && !word.fromDict &&
                    word.pronunciation.empty()) {
                    word.pronunciation = word.lyric;
                    word.candidates    = {word.lyric};
                    word.errorType     = srt::g2p::DriverUnavailable;
                }
            }
            return;
        }

        // 收集需要模型推理的词：未命中词典的词（含带连字符的词）
        // 作为整体送入模型，与其他流程一致。
        std::vector<size_t>      needInferenceIndices;
        std::vector<std::string> needInferenceWords;

        for (size_t i = 0; i < context.words().size(); ++i) {
            auto &word = context.words()[i];

            if (word.mode != srt::g2p::kG2pModeConvert || word.discard || word.fromDict ||
                !word.pronunciation.empty()) {
                continue;
            }

            // 使用清洗后的词（如果有）
            std::string raw = word.cleanedLyric.empty() ? word.lyric : word.cleanedLyric;
            needInferenceWords.push_back(raw);
            needInferenceIndices.push_back(i);
        }

        // 推理结果（flat，与 needInferenceWords 对齐）
        std::vector<std::string>              prons(needInferenceWords.size());
        std::vector<std::vector<std::string>> cands(needInferenceWords.size());
        std::vector<srt::g2p::G2pErrorType>   errors(needInferenceWords.size(), srt::g2p::NoError);

        // 批量处理
        for (size_t batchStart = 0; batchStart < needInferenceWords.size(); batchStart += m_batchSize) {
            size_t batchEnd = std::min(batchStart + m_batchSize, needInferenceWords.size());

            std::vector<std::string> batchWords(needInferenceWords.begin() + batchStart,
                                                needInferenceWords.begin() + batchEnd);

            processBatch(onnxTask, batchWords, prons, cands, errors);
        }

        // 结果映射回原始位置
        for (size_t k = 0; k < needInferenceIndices.size(); ++k) {
            size_t originalIndex = needInferenceIndices[k];
            auto  &word          = context.words()[originalIndex];

            if (!prons[k].empty()) {
                word.pronunciation = trimWhitespace(prons[k]);
                word.candidates    = cands[k];
                word.errorType     = errors[k];
                word.fromModel     = true;
            } else {
                word.pronunciation = word.lyric;
                word.candidates    = {word.lyric};
                word.errorType =
                    errors[k] != srt::g2p::NoError ? errors[k] : srt::g2p::ModelInferenceFailed;
            }
        }
    }

    void ModelStep::processBatch(
        const srt::core::NO<srt::g2p::Task>                                        &task,
        const std::vector<std::string>                                             &words,
        std::vector<std::string>                                                   &prons,
        std::vector<std::vector<std::string>>                                      &cands,
        std::vector<srt::g2p::G2pErrorType>                                        &errors) {
        // 创建批量输入
        auto batchInput = srt::core::NO<srt::g2p::G2pInputV1>::create();
        for (const auto &word : words) {
            batchInput->g2pInput.push_back(word);
        }
        if (!m_langRef.empty()) {
            batchInput->languageId = m_langRef;
        }

        // 调用模型
        auto resultExp = task->start(batchInput);
        if (!resultExp) {
            // 批量转换失败，标记所有片段失败（调用方回退）
            for (size_t i = 0; i < words.size(); ++i) {
                prons[i] = "";
                cands[i].clear();
                errors[i] = srt::g2p::ModelInferenceFailed;
            }
            return;
        }

        auto result = resultExp.take();
        if (const auto g2pResult = result.as<srt::g2p::G2pResultV1>()) {
            for (size_t i = 0; i < words.size(); ++i) {
                if (i < g2pResult->g2pResult.size()) {
                    const auto &resultWord = g2pResult->g2pResult[i];
                    prons[i]               = resultWord.pronunciation;
                    cands[i]               = resultWord.candidates;
                    errors[i]              = resultWord.errorType;
                } else {
                    // 结果数量不匹配
                    prons[i] = "";
                    cands[i].clear();
                    errors[i] = srt::g2p::ModelInferenceFailed;
                }
            }
        } else {
            // 返回结果类型错误
            for (size_t i = 0; i < words.size(); ++i) {
                prons[i] = "";
                cands[i].clear();
                errors[i] = srt::g2p::ModelInferenceFailed;
            }
        }
    }

} // namespace srt::g2p::plugins::ChainG2p
