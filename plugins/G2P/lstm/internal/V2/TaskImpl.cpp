#include "TaskImpl.h"

#include <mutex>
#include <shared_mutex>

#include <stdcorelib/path.h>
#include <stdcorelib/pimpl.h>
#include <stdcorelib/str.h>

#include <synthrt/Core/Tensor/Tensor.h>
#include <synthrt/G2P/Task/Task.h>
#include <synthrt/G2P/Task/TaskPlugin.h>

#include <InferUtil/TensorHelper.h>

#include <synthrt/G2P/Task/G2pTask.h>

namespace srt::g2p::plugins::LstmG2p::Internal::V2
{
    // Helper class for inference
    namespace InferenceHelper
    {
        // 批量预处理：将多个单词填充到相同长度，形成 (batch_size, max_seq_len) 的输入
        static srt::core::Expected<srt::core::NO<srt::core::ITensor>>
        preprocessBatch(const std::vector<std::string> &words, const std::map<std::string, int> &charVocab, const int bosIdx,
                        const int eosIdx, const int unkIdx, const int padIdx) {
            if (words.empty()) {
                return srt::g2p::Error(srt::g2p::Error::ConfigError, "words list is empty");
            }

            // 1. 将每个单词转换为索引序列
            std::vector<std::vector<int64_t>> sequences;
            sequences.reserve(words.size());

            for (const auto &word : words) {
                std::string processedWord = stdc::to_lower(word);
                stdc::trim(processedWord);

                std::vector<int64_t> indices;
                indices.push_back(bosIdx); // BOS

                for (const char c : processedWord) {
                    std::string charStr(1, c);
                    if (auto it = charVocab.find(charStr); it != charVocab.end()) {
                        indices.push_back(it->second);
                    } else {
                        indices.push_back(unkIdx);
                    }
                }

                indices.push_back(eosIdx); // EOS
                sequences.push_back(std::move(indices));
            }

            // 2. 找到最大序列长度
            size_t maxLen = 0;
            for (const auto &seq : sequences) {
                maxLen = std::max(maxLen, seq.size());
            }

            // 3. 填充所有序列到相同长度
            const size_t batchSize = words.size();
            std::vector<int64_t> padded(batchSize * maxLen, padIdx);

            for (size_t i = 0; i < batchSize; ++i) {
                const auto &seq = sequences[i];
                for (size_t j = 0; j < seq.size(); ++j) {
                    padded[i * maxLen + j] = seq[j];
                }
            }

            // 4. 创建张量，形状为 (batch_size, max_seq_len)
            const std::vector<int64_t> shape{static_cast<int64_t>(batchSize), static_cast<int64_t>(maxLen)};
            if (auto exp = srt::core::Tensor::createFromView<int64_t>(shape, stdc::array_view<int64_t>{padded}); exp) {
                return exp.take();
            }
            return srt::g2p::Error(srt::g2p::Error::ConfigError,
                                   stdc::formatN("Failed to create batch tensor for %1 words", words.size()));
        }

        // 批量解码：将 token ids 转换为音素列表
        static std::vector<std::vector<std::string>>
        decodePhonemesBatch(const std::vector<std::vector<int64_t>> &batchIds,
                            const std::map<int, std::string> &idxToPhoneme, const int bosIdx, const int eosIdx,
                            const int padIdx, const int unkIdx) {
            std::vector<std::vector<std::string>> result;
            result.reserve(batchIds.size());

            for (const auto &ids : batchIds) {
                std::vector<std::string> phonemes;
                for (const int64_t id : ids) {
                    // Skip special tokens
                    if (id == bosIdx || id == eosIdx || id == padIdx || id == unkIdx) {
                        continue;
                    }

                    auto it = idxToPhoneme.find(static_cast<int>(id));
                    if (it != idxToPhoneme.end()) {
                        phonemes.push_back(it->second);
                    }
                }
                result.push_back(std::move(phonemes));
            }

            return result;
        }
    } // namespace InferenceHelper

    srt::core::Expected<srt::core::NO<srt::g2p::TaskResult>>
    LstmG2pTaskImpl::start(const srt::core::NO<srt::g2p::TaskInput> &input) {
        if (!input)
            return srt::g2p::Error(srt::g2p::Error::ConfigError, "g2p input is nullptr");

        const auto g2pInput = input.as<srt::g2p::G2pInputV1>();

        // 预检查输入
        if (g2pInput->g2pInput.empty())
            return srt::g2p::Error(srt::g2p::Error::ConfigError, "input words are empty");

        // Driver unavailable — graceful degradation
        if (!m_driverAvailable) {
            return makeFallbackResult(g2pInput->g2pInput);
        }

        {
            std::shared_lock lock(m_mutex);
            if (!m_driver)
                return srt::g2p::Error(srt::g2p::Error::RuntimeError, "inference driver not initialized");
        }

        const size_t batchSize = g2pInput->g2pInput.size();

        // 安全性检查：限制批量大小，避免内存问题
        const size_t maxBatchSize = 256;  // 最大批量大小限制
        if (batchSize > maxBatchSize) {
            return srt::g2p::Error(srt::g2p::Error::ConfigError,
                                 stdc::formatN("Batch size too large: %1 (maximum allowed: %2)", batchSize, maxBatchSize),
                                 "Split your input into smaller batches");
        }

        // 检查 session 是否已初始化
        {
            std::shared_lock lock(m_mutex);
            if (!m_encoderSession || !m_encoderSession->isOpen() || !m_decodeSession || !m_decodeSession->isOpen()) {
                return srt::g2p::Error(srt::g2p::Error::RuntimeError, "encoder or decoder session is not initialized");
            }
        }

        // 1. 批量预处理
        auto preprocessedBatchExp =
            InferenceHelper::preprocessBatch(g2pInput->g2pInput, m_charVocab, m_bosIdx, m_eosIdx, m_unkIdx, m_padIdx);
        if (!preprocessedBatchExp) {
            return preprocessedBatchExp.takeError();
        }
        auto preprocessedBatch = preprocessedBatchExp.take();

        // 2. 编码器推理（只调用一次）
        auto encoderInput = srt::core::NO<srt::g2p::SessionStartInput>::create();
        encoderInput->inputs["input_ids"] = preprocessedBatch;

        encoderInput->outputs.insert("encoder_outputs");
        encoderInput->outputs.insert("hidden");
        encoderInput->outputs.insert("cell");

        srt::core::NO<srt::g2p::SessionResult> encoderResult;
        {
            std::unique_lock lock(m_mutex);
            auto encoderExp = m_encoderSession->start(encoderInput);
            if (!encoderExp) {
                return encoderExp.takeError();
            }
            auto sessionTaskResult = encoderExp.take();
            if (!sessionTaskResult) {
                return srt::g2p::Error(srt::g2p::Error::RuntimeError, "invalid encoder result");
            }
            encoderResult = sessionTaskResult.as<srt::g2p::SessionResult>();
        }

        // 提取编码器输出
        auto encoderOutputs = getTensorFromResult(encoderResult, "encoder_outputs");
        auto hidden = getTensorFromResult(encoderResult, "hidden");
        auto cell = getTensorFromResult(encoderResult, "cell");

        if (!encoderOutputs || !hidden || !cell) {
            std::string errMsg = "failed to get encoder outputs: ";
            if (!encoderOutputs) errMsg += "encoder_outputs is null; ";
            if (!hidden) errMsg += "hidden is null; ";
            if (!cell) errMsg += "cell is null; ";
            return srt::g2p::Error(srt::g2p::Error::RuntimeError, errMsg);
        }

        // 保存张量所有权
        auto savedEncoderOutputs = encoderOutputs.take();
        auto savedCurrentHidden = hidden.take();
        auto savedCurrentCell = cell.take();

        // 3. 解码器推理（循环生成）
        // 初始化 decoder input: (batch_size, 1)
        std::vector<int64_t> decoderInitData(batchSize, m_bosIdx);
        const std::vector<int64_t> decoderInitShape{static_cast<int64_t>(batchSize), 1};
        auto decoderInput =
            srt::core::Tensor::createFromView<int64_t>(decoderInitShape, stdc::array_view<int64_t>{decoderInitData});
        if (!decoderInput) {
            return decoderInput.takeError();
        }

        // 跟踪每个样本是否完成
        std::vector<bool> finished(batchSize, false);
        std::vector<std::vector<int64_t>> allPredictions(batchSize);

        auto currentHidden = savedCurrentHidden;
        auto currentCell = savedCurrentCell;

        const int64_t maxLen = m_maxLen > 0 ? m_maxLen : 48;

        for (int64_t step = 0; step < maxLen; ++step) {
            // 检查是否所有样本都已完成
            bool allFinished = true;
            for (const auto &f : finished) {
                if (!f) {
                    allFinished = false;
                    break;
                }
            }
            if (allFinished) {
                break;
            }

            // 识别活跃样本
            std::vector<size_t> activeIndices;
            for (size_t i = 0; i < batchSize; ++i) {
                if (!finished[i]) {
                    activeIndices.push_back(i);
                }
            }

            if (activeIndices.empty()) {
                break;
            }

            // 对活跃样本进行解码
            auto decoderSessionInput = srt::core::NO<srt::g2p::SessionStartInput>::create();
            decoderSessionInput->inputs["decoder_input"] = decoderInput.take();
            decoderSessionInput->inputs["hidden"] = currentHidden;
            decoderSessionInput->inputs["cell"] = currentCell;
            decoderSessionInput->inputs["encoder_outputs"] = savedEncoderOutputs;

            decoderSessionInput->outputs.insert("output");
            decoderSessionInput->outputs.insert("hidden_new");
            decoderSessionInput->outputs.insert("cell_new");
            decoderSessionInput->outputs.insert("attention_weights");

            srt::core::NO<srt::g2p::SessionResult> decoderResult;
            {
                std::unique_lock lock(m_mutex);
                auto decoderExp = m_decodeSession->start(decoderSessionInput);
                if (!decoderExp) {
                    return decoderExp.takeError();
                }
                auto sessionTaskResult = decoderExp.take();
                if (!sessionTaskResult) {
                    return srt::g2p::Error(srt::g2p::Error::RuntimeError, "invalid decoder result");
                }
                decoderResult = sessionTaskResult.as<srt::g2p::SessionResult>();
            }

            // 提取输出
            auto output = getTensorFromResult(decoderResult, "output");
            auto hiddenNew = getTensorFromResult(decoderResult, "hidden_new");
            auto cellNew = getTensorFromResult(decoderResult, "cell_new");

            if (!output || !hiddenNew || !cellNew) {
                std::string errMsg = "failed to get decoder outputs at step " + std::to_string(step) + ": ";
                if (!output) errMsg += "output is null; ";
                if (!hiddenNew) errMsg += "hidden_new is null; ";
                if (!cellNew) errMsg += "cell_new is null; ";
                return srt::g2p::Error(srt::g2p::Error::RuntimeError, errMsg);
            }

            // 获取 output 的数据视图（不转移所有权）
            const auto outputTensor = output.value();
            if (outputTensor->dataType() != srt::core::ITensor::Float) {
                return srt::g2p::Error(srt::g2p::Error::RuntimeError, "decoder output is not float");
            }

            auto outputView = outputTensor->view<float>();
            if (outputView.empty()) {
                return srt::g2p::Error(srt::g2p::Error::RuntimeError, "decoder output is empty");
            }

            // 获取每个样本的预测 ID
            const int64_t vocabSize = static_cast<int64_t>(outputView.size() / batchSize);
            std::vector<int64_t> predictedIds(batchSize);

            for (size_t i = 0; i < batchSize; ++i) {
                float maxProb = outputView[i * vocabSize];
                int64_t predictedId = 0;
                for (int64_t j = 1; j < vocabSize; ++j) {
                    const float prob = outputView[i * vocabSize + j];
                    if (prob > maxProb) {
                        maxProb = prob;
                        predictedId = j;
                    }
                }
                predictedIds[i] = predictedId;
            }

            // 更新状态并记录预测
            for (size_t i = 0; i < batchSize; ++i) {
                if (!finished[i]) {
                    if (predictedIds[i] == m_eosIdx) {
                        finished[i] = true;
                    } else {
                        allPredictions[i].push_back(predictedIds[i]);
                    }
                }
            }

            // 准备下一步的 decoder input
            std::vector<int64_t> nextInputData(batchSize);
            for (size_t i = 0; i < batchSize; ++i) {
                nextInputData[i] = predictedIds[i];
            }

            auto nextInput =
                srt::core::Tensor::createFromView<int64_t>(decoderInitShape, stdc::array_view<int64_t>{nextInputData});
            if (!nextInput) {
                return nextInput.takeError();
            }
            decoderInput = nextInput.value();

            // 更新 hidden 和 cell
            currentHidden = hiddenNew.take();
            currentCell = cellNew.take();

            // 检查是否所有样本都已完成
            allFinished = true;
            for (const auto &f : finished) {
                if (!f) {
                    allFinished = false;
                    break;
                }
            }
            if (allFinished) {
                break;
            }
        }

        // 4. 解码音素
        auto phonemesBatch = InferenceHelper::decodePhonemesBatch(allPredictions, m_idxToPhoneme, m_bosIdx, m_eosIdx, m_padIdx, m_unkIdx);

        // 5. 创建结果
        auto g2pResult = srt::core::NO<srt::g2p::G2pResultV1>::create();
        g2pResult->g2pResult.reserve(batchSize);

        for (size_t i = 0; i < batchSize; ++i) {
            const auto &lyric = g2pInput->g2pInput[i];
            const auto &phonemes = phonemesBatch[i];

            std::string pronStr;
            for (const auto &phone : phonemes) {
                pronStr += phone + " ";
            }

            if (phonemes.empty()) {
                g2pResult->g2pResult.emplace_back(srt::g2p::G2pRes{
                    std::string(lyric), std::string(m_spec->id()), std::string(), stdc::VersionNumber{}, std::string(lyric), std::vector<std::string>(),
                    std::string(srt::g2p::kG2pModeCopy), srt::g2p::PhonemeGenerationFailed, std::string()});
            } else {
                g2pResult->g2pResult.emplace_back(srt::g2p::G2pRes{std::string(lyric), std::string(m_spec->id()),
                                                                   std::string(), stdc::VersionNumber{}, std::string(pronStr), std::vector<std::string>(),
                                                                   std::string(srt::g2p::kG2pModeConvert)});
            }
        }

        return g2pResult;
    }

} // namespace srt::g2p::plugins::LstmG2p::Internal::V2
