#include "ModelStep.h"

#include <synthrt/Core/Support/ConfigAccessor.h>
#include <synthrt/Core/Support/Logging.h>
#include <synthrt/G2P/Base/LangCommon.h>
#include <synthrt/G2P/Core/PackageManager.h>
#include <synthrt/G2P/Support/ContextUtils.h>
#include <synthrt/G2P/Support/Error.h>

#include <algorithm>
#include <cctype>
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

        // 解析可选字典 file —— 供连字符词（如 hello-world）的片段优先查字典，
        // 字典查不到的片段才走 ONNX 推理。加载失败不阻塞：退化为全模型推理。
        auto fileIt = config.find("file");
        if (fileIt != config.end() && fileIt->second.isString()) {
            auto cfg = srt::core::ConfigAccessor(config, spec->path());
            auto dictPathExp = cfg.getResolvedPath("file");
            if (!dictPathExp) {
                ModelLog.srtWarning("Model step: optional dict path resolution failed: %1",
                                    dictPathExp.error().message());
            } else {
                std::error_code ec;
                if (m_phonemeDict.load(dictPathExp.take(), &ec)) {
                    m_dictLoaded = true;
                } else {
                    ModelLog.srtWarning("Model step: optional dict load failed (error %1); "
                                        "hyphen parts will use model inference only",
                                        ec.value());
                }
            }
        }

        // 惰性获取 G2p 任务（graceful degradation）
        // NOTE: Source used spec->Mgr(); synthrt's ModuleSpec lacks Mgr(), so
        // we use m_task->Mgr() (Task has Mgr() that returns the PackageManager).
        srt::g2p::PackageManager *mgr = m_task ? m_task->Mgr() : nullptr;
        if (!mgr) {
            ModelLog.srtWarning("Model step: PackageManager unavailable, "
                                "model inference will be disabled for '%1'",
                                m_onnxG2pId);
            m_enabled = false;
            return {};
        }

        auto g2pCate = mgr->category(srt::g2p::kG2pCategory);
        if (!g2pCate) {
            ModelLog.srtWarning("Model step: category 'g2p' not found, "
                                "model inference will be disabled for '%1'",
                                m_onnxG2pId);
            m_enabled = false;
            return {};
        }

        // FQID 两级查找：本 context 优先，找不到且非默认 context 时回退默认 context（官方兜底）
        const auto ctxKey = m_spec->contextKey();
        const auto fqid   = srt::g2p::ContextUtils::formatFqid(ctxKey, m_onnxG2pId);
        auto       g2pObj = g2pCate->getFirstObject(fqid);
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

    /// 按 '-' 拆分单词，跳过空片段；无连字符时原样返回。
    static std::vector<std::string> splitHyphen(const std::string &s) {
        std::vector<std::string> out;
        size_t                   start = 0;
        while (start <= s.size()) {
            size_t pos = s.find('-', start);
            if (pos == std::string::npos) {
                if (start < s.size()) {
                    out.push_back(s.substr(start));
                }
                break;
            }
            if (pos > start) {
                out.push_back(s.substr(start, pos - start));
            }
            start = pos + 1;
        }
        if (out.empty()) {
            out.push_back(s);
        }
        return out;
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
        if (!m_enabled || !m_onnxTask) {
            // Mark words that need inference with DriverUnavailable
            if (!m_onnxTask) {
                for (auto &word : context.words()) {
                    if (word.mode == srt::g2p::kG2pModeConvert && !word.discard && !word.fromDict &&
                        word.pronunciation.empty()) {
                        word.pronunciation = word.lyric;
                        word.candidates    = {word.lyric};
                        word.errorType     = srt::g2p::DriverUnavailable;
                    }
                }
            }
            return;
        }

        // 收集需要模型推理的词；连字符词（如 hello-world）拆分为多个片段：
        // 片段先查随附字典（配置了 file 时），字典查不到的片段才送入模型，
        // 最终合并回单个音素序列（任一片段失败则整个词失败）。
        struct PartToken {
            std::string segment;      // 字典命中的音素片段（fromDict 时有效）
            size_t      inferIndex = size_t(-1); // 模型 flat 结果下标（!fromDict 时有效）
            bool        fromDict   = false;
        };
        struct HyphenWordParts {
            size_t            originalIndex;
            std::vector<PartToken> parts;
        };

        std::vector<size_t>                    needInferenceIndices; // 普通（单片段）词的原词下标
        std::vector<std::pair<size_t, size_t>> needPartRanges;       // [word] → [start, end) of needInferenceWords
        std::vector<std::string>               needInferenceWords;  // flat 模型输入
        std::vector<HyphenWordParts>           hyphenWords;

        for (size_t i = 0; i < context.words().size(); ++i) {
            auto &word = context.words()[i];

            // 只处理需要转换、未丢弃、且字典查不到的词
            if (word.mode != srt::g2p::kG2pModeConvert || word.discard || word.fromDict ||
                !word.pronunciation.empty()) {
                continue;
            }

            // 使用清洗后的词（如果有）
            std::string raw   = word.cleanedLyric.empty() ? word.lyric : word.cleanedLyric;
            auto        parts = splitHyphen(raw);

            if (parts.size() == 1) {
                // 普通单词：直接走模型
                size_t start = needInferenceWords.size();
                needInferenceWords.push_back(parts[0]);
                needInferenceIndices.push_back(i);
                needPartRanges.emplace_back(start, start + 1);
                continue;
            }

            // 连字符词：片段先查字典，字典查不到的才入模型批
            HyphenWordParts rec;
            rec.originalIndex = i;
            for (const auto &part : parts) {
                PartToken tok;

                if (m_dictLoaded) {
                    // 字典中的词均为小写（如 cmudict），查找键转小写实现大小写不敏感匹配
                    std::string key = part;
                    std::transform(key.begin(), key.end(), key.begin(),
                                   [](unsigned char c) { return static_cast<unsigned char>(std::tolower(c)); });
                    if (const auto it = m_phonemeDict.find(key.c_str()); it != m_phonemeDict.end()) {
                        const auto phonemeVec = it->second.vec();
                        if (!phonemeVec.empty()) {
                            std::string seg;
                            for (size_t k = 0; k < phonemeVec.size(); ++k) {
                                if (k > 0) {
                                    seg += ' ';
                                }
                                seg += phonemeVec[k];
                            }
                            tok.segment  = seg;
                            tok.fromDict = true;
                            rec.parts.push_back(std::move(tok));
                            continue;
                        }
                    }
                }

                tok.inferIndex = needInferenceWords.size();
                needInferenceWords.push_back(part);
                rec.parts.push_back(std::move(tok));
            }
            hyphenWords.push_back(std::move(rec));
        }

        // 推理结果（flat，与 needInferenceWords 对齐）
        std::vector<std::string>              partProns(needInferenceWords.size());
        std::vector<std::vector<std::string>> partCands(needInferenceWords.size());
        std::vector<srt::g2p::G2pErrorType>   partErrors(needInferenceWords.size(), srt::g2p::NoError);

        // 批量处理
        for (size_t batchStart = 0; batchStart < needInferenceWords.size(); batchStart += m_batchSize) {
            size_t batchEnd = std::min(batchStart + m_batchSize, needInferenceWords.size());

            std::vector<std::string> batchWords(needInferenceWords.begin() + batchStart,
                                                needInferenceWords.begin() + batchEnd);

            processBatch(batchWords, partProns, partCands, partErrors);
        }

        // 结果映射回原始位置
        for (size_t k = 0; k < needInferenceIndices.size(); ++k) {
            size_t originalIndex = needInferenceIndices[k];
            auto  &word          = context.words()[originalIndex];
            size_t f             = needPartRanges[k].first;

            if (!partProns[f].empty()) {
                word.pronunciation = trimWhitespace(partProns[f]);
                word.candidates    = partCands[f];
                word.errorType     = partErrors[f];
                word.fromModel     = true;
            } else {
                word.pronunciation = word.lyric;
                word.candidates    = {word.lyric};
                word.errorType =
                    partErrors[f] != srt::g2p::NoError ? partErrors[f] : srt::g2p::ModelInferenceFailed;
            }
        }

        // 连字符词：全部片段成功后合并为单个音素序列；
        // 任一片段失败（字典未命中且模型推理失败）则整个词失败。
        for (auto &rec : hyphenWords) {
            auto &word = context.words()[rec.originalIndex];

            bool                     failed   = false;
            srt::g2p::G2pErrorType   failError = srt::g2p::NoError;
            for (const auto &tok : rec.parts) {
                if (tok.fromDict) {
                    continue;
                }
                if (partProns[tok.inferIndex].empty() || partErrors[tok.inferIndex] != srt::g2p::NoError) {
                    failed = true;
                    failError = partErrors[tok.inferIndex] != srt::g2p::NoError
                                    ? partErrors[tok.inferIndex]
                                    : srt::g2p::ModelInferenceFailed;
                    break;
                }
            }

            if (failed) {
                word.pronunciation = word.lyric;
                word.candidates    = {word.lyric};
                word.errorType     = failError;
                continue;
            }

            // 全部成功：按原顺序合并为单一音素序列
            std::string merged;
            bool        anyDict = false, anyModel = false;
            for (const auto &tok : rec.parts) {
                const std::string seg =
                    tok.fromDict ? tok.segment : trimWhitespace(partProns[tok.inferIndex]);
                if (!merged.empty()) {
                    merged += ' ';
                }
                merged += seg;

                if (tok.fromDict) {
                    anyDict = true;
                } else {
                    anyModel = true;
                }
            }
            word.pronunciation = merged;
            word.candidates    = {merged};
            word.errorType     = srt::g2p::NoError;
            word.fromDict      = anyDict && !anyModel;
            word.fromModel     = anyModel && !anyDict;
        }
    }

    void ModelStep::processBatch(const std::vector<std::string> &words, std::vector<std::string> &prons,
                                 std::vector<std::vector<std::string>> &cands,
                                 std::vector<srt::g2p::G2pErrorType>   &errors) {
        // 创建批量输入
        auto batchInput = srt::core::NO<srt::g2p::G2pInputV1>::create();
        for (const auto &word : words) {
            batchInput->g2pInput.push_back(word);
        }
        if (!m_langRef.empty()) {
            batchInput->languageId = m_langRef;
        }

        // 调用模型
        auto resultExp = m_onnxTask->start(batchInput);
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

    void ModelStep::cleanup() {
        m_onnxTask.reset();
    }

} // namespace srt::g2p::plugins::ChainG2p
