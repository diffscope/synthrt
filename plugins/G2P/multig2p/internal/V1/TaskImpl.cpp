#include "TaskImpl.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <shared_mutex>
#include <unordered_set>

#include <stdcorelib/str.h>

#include <synthrt/Core/Tensor/Tensor.h>
#include <synthrt/G2P/Task/Task.h>
#include <synthrt/G2P/Task/G2pTask.h>

namespace srt::g2p::plugins::Multig2p::Internal::V1 {
    namespace InferenceHelper {
        // ============== 预处理 ==============

        /// 单词 → 索引序列 [BOS] + chars + [EOS]，查询 {lang}/{variant}/{symbol}。
        /// 缺失字符用 unk_idx 替换。
        static std::vector<int64_t>
        encodeWord(const std::string &word, const std::string &language,
                   const std::string &variant, const VocabularyData &vocab) {
            std::vector<int64_t> indices;
            indices.push_back(vocab.bosIdx);
            const std::string prefix = language + "/" + variant + "/";
            for (const char c : word) {
                const std::string charStr(1, c);
                // 全局符号（理论上单字符不会命中，但保留逻辑一致性）
                bool isGlobal = false;
                for (const auto &g : vocab.globalSymbols) {
                    if (g == charStr) {
                        isGlobal = true;
                        break;
                    }
                }
                if (isGlobal) {
                    const int id = vocab.lookup(charStr);
                    indices.push_back(id >= 0 ? id : vocab.unkIdx);
                } else {
                    const int id = vocab.lookup(prefix + charStr);
                    indices.push_back(id >= 0 ? id : vocab.unkIdx);
                }
            }
            indices.push_back(vocab.eosIdx);
            return indices;
        }

        /// 批量预处理：words → (src[B,Lsrc], src_pad_mask[B,Lsrc], lang_ids[B])。
        /// 所有序列用 pad_idx 填充到 max_src_len；src_pad_mask 在 pad 位置为 true。
        ///
        /// langRefs[i] 是 lang_ref 字符串（如 "eng/default"），通过 LangIdMap 查 lang_id；
        /// 缺失返回 -1（调用方决定是否报错或回退 0）。
        struct PreprocessResult {
            srt::core::NO<srt::core::ITensor> src;
            srt::core::NO<srt::core::ITensor> srcPadMask;
            srt::core::NO<srt::core::ITensor> langIds;
            std::vector<int> langIdList;       // 每个样本的 lang_id（用于 beam expand）
            std::vector<int> missingLangIndices; // lang_ref 未在 LangIdMap 中找到的样本下标
        };

        static srt::core::Expected<PreprocessResult>
        preprocessBatch(const std::vector<std::string> &words,
                        const std::vector<std::string> &langRefs,
                        const VocabularyData &vocab,
                        const LangIdMap &langIdMap,
                        const std::string &defaultLangRef) {
            if (words.empty()) {
                return srt::g2p::Error(srt::g2p::Error::ConfigError, "words list is empty");
            }
            if (words.size() != langRefs.size()) {
                return srt::g2p::Error(
                    srt::g2p::Error::ConfigError,
                    stdc::formatN("words.size()=%1 != langRefs.size()=%2",
                                  words.size(), langRefs.size()));
            }

            const size_t B = words.size();
            std::vector<std::vector<int64_t>> sequences;
            sequences.reserve(B);
            std::vector<int> langIdList(B, 0);
            std::vector<int> missingLangIndices;

            // 语言前缀拆分（langRef = "lang/variant"）
            auto splitLangRef = [](const std::string &ref) -> std::pair<std::string, std::string> {
                const auto pos = ref.find('/');
                if (pos == std::string::npos) {
                    return {ref, "default"};
                }
                return {ref.substr(0, pos), ref.substr(pos + 1)};
            };

            size_t maxLen = 0;
            for (size_t i = 0; i < B; ++i) {
                const auto [lang, variant] = splitLangRef(langRefs[i]);
                auto seq = encodeWord(words[i], lang, variant, vocab);
                maxLen = std::max(maxLen, seq.size());
                sequences.push_back(std::move(seq));

                const int langId = langIdMap.lookup(langRefs[i]);
                if (langId < 0) {
                    missingLangIndices.push_back(static_cast<int>(i));
                    // 回退到 defaultLangRef
                    const int fallbackId = langIdMap.lookup(defaultLangRef);
                    langIdList[i] = fallbackId >= 0 ? fallbackId : 0;
                } else {
                    langIdList[i] = langId;
                }
            }

            // 构造 [B, maxLen] int64 张量（pad_idx 填充） + [B, maxLen] bool 张量
            std::vector<int64_t> srcData(B * maxLen, vocab.padIdx);
            std::vector<uint8_t> maskData(B * maxLen, 1); // true = pad
            for (size_t i = 0; i < B; ++i) {
                const auto &seq = sequences[i];
                for (size_t j = 0; j < seq.size(); ++j) {
                    srcData[i * maxLen + j] = seq[j];
                    maskData[i * maxLen + j] = 0; // false = 非pad
                }
            }

            const std::vector<int64_t> srcShape{static_cast<int64_t>(B), static_cast<int64_t>(maxLen)};
            auto srcExp = srt::core::Tensor::createFromView<int64_t>(srcShape, stdc::array_view<int64_t>{srcData});
            if (!srcExp) return srcExp.takeError();

            const std::vector<int64_t> maskShape{static_cast<int64_t>(B), static_cast<int64_t>(maxLen)};
            auto maskExp = srt::core::Tensor::createFromView<uint8_t>(maskShape, stdc::array_view<uint8_t>{maskData});
            if (!maskExp) return maskExp.takeError();

            std::vector<int64_t> langIdsData(B);
            for (size_t i = 0; i < B; ++i) langIdsData[i] = langIdList[i];
            const std::vector<int64_t> langIdsShape{static_cast<int64_t>(B)};
            auto langIdsExp = srt::core::Tensor::createFromView<int64_t>(langIdsShape, stdc::array_view<int64_t>{langIdsData});
            if (!langIdsExp) return langIdsExp.takeError();

            PreprocessResult r;
            r.src = srcExp.take();
            r.srcPadMask = maskExp.take();
            r.langIds = langIdsExp.take();
            r.langIdList = std::move(langIdList);
            r.missingLangIndices = std::move(missingLangIndices);
            return r;
        }

        // ============== Encoder 调用 ==============

        static srt::core::Expected<srt::core::NO<srt::core::ITensor>>
        runEncoder(const srt::core::NO<srt::g2p::SessionTask> &session,
                   const srt::core::NO<srt::core::ITensor> &src,
                   const srt::core::NO<srt::core::ITensor> &langIds,
                   const srt::core::NO<srt::core::ITensor> &srcPadMask) {
            auto input = srt::core::NO<srt::g2p::SessionStartInput>::create();
            input->inputs["src"] = src;
            input->inputs["lang_ids"] = langIds;
            input->inputs["src_pad_mask"] = srcPadMask;
            input->outputs.insert("encoder_out");

            auto exp = session->start(input);
            if (!exp) return exp.takeError();
            auto taskResult = exp.take();
            if (!taskResult) {
                return srt::g2p::Error(srt::g2p::Error::RuntimeError, "invalid encoder result");
            }
            auto sessionResult = taskResult.as<srt::g2p::SessionResult>();
            return getTensorFromResult(sessionResult, "encoder_out");
        }

        // ============== Greedy + kv_cache 解码 ==============

        struct GreedyState {
            srt::core::NO<srt::core::ITensor> kvK, kvV, kvCk, kvCv;
        };

        /// decoder_step_init 调用：输入 dec_input[B,1]=BOS + lang_ids + encoder_out + src_pad_mask
        /// 输出 logits + new_kv_cache_k/v + new_kv_cache_cross_k/v
        static srt::core::Expected<GreedyState>
        runStepInit(const srt::core::NO<srt::g2p::SessionTask> &session,
                    const srt::core::NO<srt::core::ITensor> &decInput,
                    const srt::core::NO<srt::core::ITensor> &langIds,
                    const srt::core::NO<srt::core::ITensor> &encoderOut,
                    const srt::core::NO<srt::core::ITensor> &srcPadMask,
                    srt::core::NO<srt::core::ITensor> &logitsOut) {
            auto input = srt::core::NO<srt::g2p::SessionStartInput>::create();
            input->inputs["dec_input"] = decInput;
            input->inputs["lang_ids"] = langIds;
            input->inputs["encoder_out"] = encoderOut;
            input->inputs["src_pad_mask"] = srcPadMask;
            input->outputs.insert("logits");
            input->outputs.insert("new_kv_cache_k");
            input->outputs.insert("new_kv_cache_v");
            input->outputs.insert("new_kv_cache_cross_k");
            input->outputs.insert("new_kv_cache_cross_v");

            auto exp = session->start(input);
            if (!exp) return exp.takeError();
            auto taskResult = exp.take();
            if (!taskResult) {
                return srt::g2p::Error(srt::g2p::Error::RuntimeError, "invalid step_init result");
            }
            auto sessionResult = taskResult.as<srt::g2p::SessionResult>();

            GreedyState state;
            auto logits = getTensorFromResult(sessionResult, "logits");
            if (!logits) return logits.takeError();
            logitsOut = logits.take();

            auto kvK = getTensorFromResult(sessionResult, "new_kv_cache_k");
            auto kvV = getTensorFromResult(sessionResult, "new_kv_cache_v");
            auto kvCk = getTensorFromResult(sessionResult, "new_kv_cache_cross_k");
            auto kvCv = getTensorFromResult(sessionResult, "new_kv_cache_cross_v");
            if (!kvK || !kvV || !kvCk || !kvCv) {
                return srt::g2p::Error(srt::g2p::Error::RuntimeError,
                                       "step_init missing kv_cache outputs");
            }
            state.kvK = kvK.take();
            state.kvV = kvV.take();
            state.kvCk = kvCk.take();
            state.kvCv = kvCv.take();
            return state;
        }

        /// decoder_step 调用：输入 dec_input[B,1] + lang_ids + src_pad_mask + 4 个 kv_cache
        /// 输出 logits + new_kv_cache_k/v + new_kv_cache_cross_k/v（cross 透传不变）
        static srt::core::Expected<GreedyState>
        runStep(const srt::core::NO<srt::g2p::SessionTask> &session,
                const srt::core::NO<srt::core::ITensor> &decInput,
                const srt::core::NO<srt::core::ITensor> &langIds,
                const srt::core::NO<srt::core::ITensor> &srcPadMask,
                const GreedyState &prev,
                srt::core::NO<srt::core::ITensor> &logitsOut) {
            auto input = srt::core::NO<srt::g2p::SessionStartInput>::create();
            input->inputs["dec_input"] = decInput;
            input->inputs["lang_ids"] = langIds;
            input->inputs["src_pad_mask"] = srcPadMask;
            input->inputs["kv_cache_k"] = prev.kvK;
            input->inputs["kv_cache_v"] = prev.kvV;
            input->inputs["kv_cache_cross_k"] = prev.kvCk;
            input->inputs["kv_cache_cross_v"] = prev.kvCv;
            input->outputs.insert("logits");
            input->outputs.insert("new_kv_cache_k");
            input->outputs.insert("new_kv_cache_v");
            input->outputs.insert("new_kv_cache_cross_k");
            input->outputs.insert("new_kv_cache_cross_v");

            auto exp = session->start(input);
            if (!exp) return exp.takeError();
            auto taskResult = exp.take();
            if (!taskResult) {
                return srt::g2p::Error(srt::g2p::Error::RuntimeError, "invalid step result");
            }
            auto sessionResult = taskResult.as<srt::g2p::SessionResult>();

            GreedyState state;
            auto logits = getTensorFromResult(sessionResult, "logits");
            if (!logits) return logits.takeError();
            logitsOut = logits.take();

            auto kvK = getTensorFromResult(sessionResult, "new_kv_cache_k");
            auto kvV = getTensorFromResult(sessionResult, "new_kv_cache_v");
            auto kvCk = getTensorFromResult(sessionResult, "new_kv_cache_cross_k");
            auto kvCv = getTensorFromResult(sessionResult, "new_kv_cache_cross_v");
            if (!kvK || !kvV || !kvCk || !kvCv) {
                return srt::g2p::Error(srt::g2p::Error::RuntimeError,
                                       "step missing kv_cache outputs");
            }
            state.kvK = kvK.take();
            state.kvV = kvV.take();
            state.kvCk = kvCk.take();
            state.kvCv = kvCv.take();
            return state;
        }

        /// 从 logits[B, 1, V] 取 argmax → next_token[B]。
        /// 返回 int64 张量 [B, 1]。
        static srt::core::Expected<srt::core::NO<srt::core::ITensor>>
        argmaxToken(const srt::core::NO<srt::core::ITensor> &logits, int B, int V) {
            if (logits->dataType() != srt::core::ITensor::Float) {
                return srt::g2p::Error(srt::g2p::Error::RuntimeError,
                                       "logits is not float");
            }
            auto view = logits->view<float>();
            if (view.empty()) {
                return srt::g2p::Error(srt::g2p::Error::RuntimeError, "logits view is empty");
            }
            std::vector<int64_t> next(B);
            for (int i = 0; i < B; ++i) {
                float maxProb = view[i * V];
                int64_t predictedId = 0;
                for (int j = 1; j < V; ++j) {
                    const float p = view[i * V + j];
                    if (p > maxProb) {
                        maxProb = p;
                        predictedId = j;
                    }
                }
                next[i] = predictedId;
            }
            const std::vector<int64_t> shape{static_cast<int64_t>(B), 1};
            return srt::core::Tensor::createFromView<int64_t>(shape, stdc::array_view<int64_t>{next});
        }

        /// 取 logits 第 t 步（最后一行）的 vocab 投影。[B, V] → 通过 view 索引
        /// 返回浮点数组（连续 B*V 个 float，row-major）。
        static srt::core::Expected<std::vector<float>>
        logitsLastStep(const srt::core::NO<srt::core::ITensor> &logits, int B, int V) {
            if (logits->dataType() != srt::core::ITensor::Float) {
                return srt::g2p::Error(srt::g2p::Error::RuntimeError, "logits is not float");
            }
            auto view = logits->view<float>();
            if (view.empty()) {
                return srt::g2p::Error(srt::g2p::Error::RuntimeError, "logits view is empty");
            }
            // logits shape: [B, T, V]，T=1，所以取全部 [B*V]
            std::vector<float> out(view.begin(), view.end());
            (void)B;
            (void)V;
            return out;
        }

        // ============== Greedy 主流程 ==============

        struct DecodeResult {
            std::vector<std::vector<int64_t>> tokens; // B 个序列（含 BOS，不含 EOS 后内容）
        };

        static srt::core::Expected<DecodeResult>
        decodeGreedy(const srt::core::NO<srt::g2p::SessionTask> &encoderSession,
                     const srt::core::NO<srt::g2p::SessionTask> &stepInitSession,
                     const srt::core::NO<srt::g2p::SessionTask> &stepSession,
                     const srt::core::NO<srt::core::ITensor> &src,
                     const srt::core::NO<srt::core::ITensor> &langIds,
                     const srt::core::NO<srt::core::ITensor> &srcPadMask,
                     const VocabularyData &vocab, int maxLen) {
            const int B = static_cast<int>(src->shape()[0]);
            const int V = static_cast<int>(vocab.symbols.size());

            // 1. Encoder 一次
            auto encOut = runEncoder(encoderSession, src, langIds, srcPadMask);
            if (!encOut) return encOut.takeError();
            auto encoderOut = encOut.take();

            // 2. 初始化 dec_input = [BOS] * B，shape [B, 1]
            std::vector<int64_t> bosData(B, vocab.bosIdx);
            const std::vector<int64_t> bosShape{static_cast<int64_t>(B), 1};
            auto bosExp = srt::core::Tensor::createFromView<int64_t>(bosShape, stdc::array_view<int64_t>{bosData});
            if (!bosExp) return bosExp.takeError();
            auto decInput = bosExp.take();

            // 3. step_init 建立缓存
            srt::core::NO<srt::core::ITensor> logitsInit;
            auto stateInitExp = runStepInit(stepInitSession, decInput, langIds, encoderOut, srcPadMask, logitsInit);
            if (!stateInitExp) return stateInitExp.takeError();
            auto state = stateInitExp.take();

            // 取首步 argmax
            auto firstTokenExp = argmaxToken(logitsInit, B, V);
            if (!firstTokenExp) return firstTokenExp.takeError();
            auto nextToken = firstTokenExp.take();

            std::vector<std::vector<int64_t>> allPredictions(B);
            std::vector<bool> finished(B, false);

            // 处理首步 token
            auto nextView = nextToken->view<int64_t>();
            for (int i = 0; i < B; ++i) {
                if (nextView[i] == vocab.eosIdx) {
                    finished[i] = true;
                } else {
                    allPredictions[i].push_back(nextView[i]);
                }
            }

            // 4. step 循环（maxLen - 1 次）
            for (int step = 1; step < maxLen; ++step) {
                bool allDone = true;
                for (const auto &f : finished) if (!f) { allDone = false; break; }
                if (allDone) break;

                // 构造 dec_input = nextToken（已 finished 的样本用 pad_idx）
                std::vector<int64_t> stepData(B);
                for (int i = 0; i < B; ++i) {
                    stepData[i] = finished[i] ? vocab.padIdx : nextView[i];
                }
                const std::vector<int64_t> stepShape{static_cast<int64_t>(B), 1};
                auto stepInputExp = srt::core::Tensor::createFromView<int64_t>(stepShape, stdc::array_view<int64_t>{stepData});
                if (!stepInputExp) return stepInputExp.takeError();
                auto stepInput = stepInputExp.take();

                srt::core::NO<srt::core::ITensor> logitsStep;
                auto stateExp = runStep(stepSession, stepInput, langIds, srcPadMask, state, logitsStep);
                if (!stateExp) return stateExp.takeError();
                state = stateExp.take();

                auto tokenExp = argmaxToken(logitsStep, B, V);
                if (!tokenExp) return tokenExp.takeError();
                nextToken = tokenExp.take();
                nextView = nextToken->view<int64_t>();

                for (int i = 0; i < B; ++i) {
                    if (!finished[i]) {
                        if (nextView[i] == vocab.eosIdx) {
                            finished[i] = true;
                        } else {
                            allPredictions[i].push_back(nextView[i]);
                        }
                    }
                }
            }

            return DecodeResult{std::move(allPredictions)};
        }

        // ============== Beam Search 主流程 ==============

        /// Beam search + kv_cache（参考 inference/decoder.py:BatchedBeamSearchDecoder）
        /// 输入 batch=B，beam_size=K，内部展开为 B*K
        static srt::core::Expected<DecodeResult>
        decodeBeam(const srt::core::NO<srt::g2p::SessionTask> &encoderSession,
                   const srt::core::NO<srt::g2p::SessionTask> &stepInitSession,
                   const srt::core::NO<srt::g2p::SessionTask> &stepSession,
                   const srt::core::NO<srt::core::ITensor> &src,
                   const srt::core::NO<srt::core::ITensor> &langIds,
                   const srt::core::NO<srt::core::ITensor> &srcPadMask,
                   const VocabularyData &vocab, int maxLen,
                   int beamSize, int topK, float lengthPenalty) {
            const int B = static_cast<int>(src->shape()[0]);
            const int K = beamSize;
            const int BK = B * K;
            const int V = static_cast<int>(vocab.symbols.size());

            // 1. Encoder 一次（B），然后 expand 到 B*K
            auto encOutExp = runEncoder(encoderSession, src, langIds, srcPadMask);
            if (!encOutExp) return encOutExp.takeError();
            auto encoderOutB = encOutExp.take();
            // encoder_out shape: [B, Lsrc, H] → expand 到 [B*K, Lsrc, H]
            // 实现：通过 view 重组（B*K 行 = 每 B 行重复 K 次）
            auto encView = encoderOutB->view<float>();
            const auto &encShape = encoderOutB->shape();
            const int Lsrc = static_cast<int>(encShape[1]);
            const int H = static_cast<int>(encShape[2]);
            std::vector<float> encExpanded(BK * Lsrc * H);
            for (int b = 0; b < B; ++b) {
                for (int k = 0; k < K; ++k) {
                    const int dst = (b * K + k) * Lsrc * H;
                    const int srcIdx = b * Lsrc * H;
                    std::copy(encView.begin() + srcIdx,
                              encView.begin() + srcIdx + Lsrc * H,
                              encExpanded.begin() + dst);
                }
            }
            const std::vector<int64_t> encExpShape{static_cast<int64_t>(BK), Lsrc, H};
            auto encExpTensorExp = srt::core::Tensor::createFromView<float>(encExpShape, stdc::array_view<float>{encExpanded});
            if (!encExpTensorExp) return encExpTensorExp.takeError();
            auto encoderOut = encExpTensorExp.take();

            // src_pad_mask expand 到 B*K
            auto maskView = srcPadMask->view<uint8_t>();
            std::vector<uint8_t> maskExpanded(BK * Lsrc);
            for (int b = 0; b < B; ++b) {
                for (int k = 0; k < K; ++k) {
                    const int dst = (b * K + k) * Lsrc;
                    const int srcIdx = b * Lsrc;
                    std::copy(maskView.begin() + srcIdx,
                              maskView.begin() + srcIdx + Lsrc,
                              maskExpanded.begin() + dst);
                }
            }
            const std::vector<int64_t> maskExpShape{static_cast<int64_t>(BK), Lsrc};
            auto maskExpTensorExp = srt::core::Tensor::createFromView<uint8_t>(maskExpShape, stdc::array_view<uint8_t>{maskExpanded});
            if (!maskExpTensorExp) return maskExpTensorExp.takeError();
            auto maskExpandedTensor = maskExpTensorExp.take();

            // lang_ids expand 到 B*K
            auto langView = langIds->view<int64_t>();
            std::vector<int64_t> langExpanded(BK);
            for (int b = 0; b < B; ++b) {
                for (int k = 0; k < K; ++k) {
                    langExpanded[b * K + k] = langView[b];
                }
            }
            const std::vector<int64_t> langExpShape{static_cast<int64_t>(BK)};
            auto langExpTensorExp = srt::core::Tensor::createFromView<int64_t>(langExpShape, stdc::array_view<int64_t>{langExpanded});
            if (!langExpTensorExp) return langExpTensorExp.takeError();
            auto langIdsExpanded = langExpTensorExp.take();

            // 2. 初始化 tokens = [BOS] * B*K, scores = -inf（[::K]=0）
            std::vector<int64_t> tokensInit(BK, vocab.bosIdx);
            std::vector<float> scores(BK, -std::numeric_limits<float>::infinity());
            for (int b = 0; b < B; ++b) scores[b * K] = 0.0f;
            std::vector<bool> finished(BK, false);

            // 跟踪 tokens 历史（每步追加一列）
            std::vector<std::vector<int64_t>> tokensHistory(BK, std::vector<int64_t>{vocab.bosIdx});

            // 3. step_init（首步）
            const std::vector<int64_t> bosShape{static_cast<int64_t>(BK), 1};
            auto bosExp = srt::core::Tensor::createFromView<int64_t>(bosShape, stdc::array_view<int64_t>{tokensInit});
            if (!bosExp) return bosExp.takeError();
            auto decInput = bosExp.take();

            srt::core::NO<srt::core::ITensor> logitsInit;
            auto stateInitExp = runStepInit(stepInitSession, decInput, langIdsExpanded, encoderOut, maskExpandedTensor, logitsInit);
            if (!stateInitExp) return stateInitExp.takeError();
            auto state = stateInitExp.take();

            // 4. 首步 log_softmax + topk
            auto logitsViewExp = logitsLastStep(logitsInit, BK, V);
            if (!logitsViewExp) return logitsViewExp.takeError();
            auto logitsVec = logitsViewExp.take();

            // log_softmax
            for (int i = 0; i < BK; ++i) {
                float maxL = logitsVec[i * V];
                for (int j = 1; j < V; ++j) maxL = std::max(maxL, logitsVec[i * V + j]);
                float sum = 0.0f;
                for (int j = 0; j < V; ++j) sum += std::exp(logitsVec[i * V + j] - maxL);
                float logSum = maxL + std::log(sum);
                for (int j = 0; j < V; ++j) logitsVec[i * V + j] -= logSum;
            }
            // finished 项的 log_probs：除 pad_idx=0 其余=-inf
            for (int i = 0; i < BK; ++i) {
                if (finished[i]) {
                    for (int j = 0; j < V; ++j) logitsVec[i * V + j] = -std::numeric_limits<float>::infinity();
                    logitsVec[i * V + vocab.padIdx] = 0.0f;
                }
            }

            // cum_scores = scores[i] + log_probs[i, v]，[B, K*V] topk
            std::vector<int64_t> newTokens(BK);
            std::vector<int> parentBeams(BK);
            {
                std::vector<float> cumScores(B * K * V);
                for (int b = 0; b < B; ++b) {
                    for (int k = 0; k < K; ++k) {
                        for (int v = 0; v < V; ++v) {
                            cumScores[(b * K + k) * V + v] = scores[b * K + k] + logitsVec[(b * K + k) * V + v];
                        }
                    }
                }
                // [B, K*V] topk K
                for (int b = 0; b < B; ++b) {
                    // 收集 K*V 个候选
                    std::vector<std::pair<float, int>> cands; // (score, idx = k*V + v)
                    cands.reserve(K * V);
                    for (int i = 0; i < K * V; ++i) {
                        cands.emplace_back(cumScores[b * K * V + i], i);
                    }
                    // partial_sort 取前 K
                    std::partial_sort(cands.begin(), cands.begin() + K, cands.end(),
                                      [](const auto &a, const auto &b) { return a.first > b.first; });
                    for (int k = 0; k < K; ++k) {
                        const auto &[s, idx] = cands[k];
                        parentBeams[b * K + k] = idx / V;
                        newTokens[b * K + k] = idx % V;
                        scores[b * K + k] = s;
                    }
                }
            }

            // 重排 tokens history / finished / kv_cache
            std::vector<std::vector<int64_t>> newTokensHistory(BK);
            std::vector<bool> newFinished(BK, false);
            for (int b = 0; b < B; ++b) {
                for (int k = 0; k < K; ++k) {
                    const int newIdx = b * K + k;
                    const int parentIdx = b * K + parentBeams[newIdx];
                    newTokensHistory[newIdx] = tokensHistory[parentIdx];
                    newTokensHistory[newIdx].push_back(newTokens[newIdx]);
                    newFinished[newIdx] = finished[parentIdx] || (newTokens[newIdx] == vocab.eosIdx) || (1 >= maxLen);
                }
            }
            tokensHistory = std::move(newTokensHistory);
            finished = std::move(newFinished);

            // kv_cache 重排（按 parentBeams 索引行）
            auto reorderKv = [&](const srt::core::NO<srt::core::ITensor> &t) -> srt::core::NO<srt::core::ITensor> {
                if (!t) return {};
                const auto &shape = t->shape();
                // kv shape: [L, B, H] 或 [B, L, H]，按第 1 维（B 维）索引
                // 假设 shape[1] == BK（exporter 输出约定）
                if (shape.size() < 2) return t;
                auto view = t->view<float>();
                const int dim0 = static_cast<int>(shape[0]);
                const int dim1 = static_cast<int>(shape[1]);
                int rest = 1;
                for (size_t i = 2; i < shape.size(); ++i) rest *= static_cast<int>(shape[i]);
                std::vector<float> reordered(dim0 * dim1 * rest);
                for (int b = 0; b < B; ++b) {
                    for (int k = 0; k < K; ++k) {
                        const int newIdx = b * K + k;
                        const int parentIdx = b * K + parentBeams[newIdx];
                        for (int i = 0; i < dim0; ++i) {
                            std::copy(view.begin() + (i * dim1 + parentIdx) * rest,
                                      view.begin() + (i * dim1 + parentIdx + 1) * rest,
                                      reordered.begin() + (i * dim1 + newIdx) * rest);
                        }
                    }
                }
                auto exp = srt::core::Tensor::createFromView<float>(shape, stdc::array_view<float>{reordered});
                return exp.take();
            };
            state.kvK = reorderKv(state.kvK);
            state.kvV = reorderKv(state.kvV);
            state.kvCk = reorderKv(state.kvCk);
            state.kvCv = reorderKv(state.kvCv);

            // 5. step 循环（maxLen - 1 次）
            for (int step = 1; step < maxLen; ++step) {
                bool allDone = true;
                for (const auto &f : finished) if (!f) { allDone = false; break; }
                if (allDone) break;

                // dec_input = newTokens reshape [BK, 1]
                std::vector<int64_t> stepData(BK);
                for (int i = 0; i < BK; ++i) {
                    stepData[i] = finished[i] ? vocab.padIdx : newTokens[i];
                }
                const std::vector<int64_t> stepShape{static_cast<int64_t>(BK), 1};
                auto stepInputExp = srt::core::Tensor::createFromView<int64_t>(stepShape, stdc::array_view<int64_t>{stepData});
                if (!stepInputExp) return stepInputExp.takeError();
                auto stepInput = stepInputExp.take();

                srt::core::NO<srt::core::ITensor> logitsStep;
                auto stateExp = runStep(stepSession, stepInput, langIdsExpanded, maskExpandedTensor, state, logitsStep);
                if (!stateExp) return stateExp.takeError();
                state = stateExp.take();

                auto logitsVecExp = logitsLastStep(logitsStep, BK, V);
                if (!logitsVecExp) return logitsVecExp.takeError();
                logitsVec = logitsVecExp.take();

                // log_softmax
                for (int i = 0; i < BK; ++i) {
                    float maxL = logitsVec[i * V];
                    for (int j = 1; j < V; ++j) maxL = std::max(maxL, logitsVec[i * V + j]);
                    float sum = 0.0f;
                    for (int j = 0; j < V; ++j) sum += std::exp(logitsVec[i * V + j] - maxL);
                    float logSum = maxL + std::log(sum);
                    for (int j = 0; j < V; ++j) logitsVec[i * V + j] -= logSum;
                }
                for (int i = 0; i < BK; ++i) {
                    if (finished[i]) {
                        for (int j = 0; j < V; ++j) logitsVec[i * V + j] = -std::numeric_limits<float>::infinity();
                        logitsVec[i * V + vocab.padIdx] = 0.0f;
                    }
                }

                // cum_scores + topk
                std::vector<int64_t> stepNewTokens(BK);
                std::vector<int> stepParentBeams(BK);
                std::vector<float> cumScores(B * K * V);
                for (int b = 0; b < B; ++b) {
                    for (int k = 0; k < K; ++k) {
                        for (int v = 0; v < V; ++v) {
                            cumScores[(b * K + k) * V + v] = scores[b * K + k] + logitsVec[(b * K + k) * V + v];
                        }
                    }
                }
                for (int b = 0; b < B; ++b) {
                    std::vector<std::pair<float, int>> cands;
                    cands.reserve(K * V);
                    for (int i = 0; i < K * V; ++i) {
                        cands.emplace_back(cumScores[b * K * V + i], i);
                    }
                    std::partial_sort(cands.begin(), cands.begin() + K, cands.end(),
                                      [](const auto &a, const auto &b) { return a.first > b.first; });
                    for (int k = 0; k < K; ++k) {
                        const auto &[s, idx] = cands[k];
                        stepParentBeams[b * K + k] = idx / V;
                        stepNewTokens[b * K + k] = idx % V;
                        scores[b * K + k] = s;
                    }
                }

                // 重排
                std::vector<std::vector<int64_t>> stepNewTokensHistory(BK);
                std::vector<bool> stepNewFinished(BK, false);
                for (int b = 0; b < B; ++b) {
                    for (int k = 0; k < K; ++k) {
                        const int newIdx = b * K + k;
                        const int parentIdx = b * K + stepParentBeams[newIdx];
                        stepNewTokensHistory[newIdx] = tokensHistory[parentIdx];
                        stepNewTokensHistory[newIdx].push_back(stepNewTokens[newIdx]);
                        stepNewFinished[newIdx] = finished[parentIdx] || (stepNewTokens[newIdx] == vocab.eosIdx) || (step + 1 >= maxLen);
                    }
                }
                tokensHistory = std::move(stepNewTokensHistory);
                finished = std::move(stepNewFinished);
                newTokens = std::move(stepNewTokens);

                // kv_cache 重排
                auto reorderKvStep = [&](const srt::core::NO<srt::core::ITensor> &t) -> srt::core::NO<srt::core::ITensor> {
                    if (!t) return {};
                    const auto &shape = t->shape();
                    if (shape.size() < 2) return t;
                    auto view = t->view<float>();
                    const int dim0 = static_cast<int>(shape[0]);
                    const int dim1 = static_cast<int>(shape[1]);
                    int rest = 1;
                    for (size_t i = 2; i < shape.size(); ++i) rest *= static_cast<int>(shape[i]);
                    std::vector<float> reordered(dim0 * dim1 * rest);
                    for (int b = 0; b < B; ++b) {
                        for (int k = 0; k < K; ++k) {
                            const int newIdx = b * K + k;
                            const int parentIdx = b * K + stepParentBeams[newIdx];
                            for (int i = 0; i < dim0; ++i) {
                                std::copy(view.begin() + (i * dim1 + parentIdx) * rest,
                                          view.begin() + (i * dim1 + parentIdx + 1) * rest,
                                          reordered.begin() + (i * dim1 + newIdx) * rest);
                            }
                        }
                    }
                    auto exp = srt::core::Tensor::createFromView<float>(shape, stdc::array_view<float>{reordered});
                    return exp.take();
                };
                bool anyUnfinished = false; for (auto f : finished) if (!f) { anyUnfinished = true; break; }
                    if (anyUnfinished) {
                    // 仅在还有未完成样本时重排（避免最后一步无意义重排）
                    state.kvK = reorderKvStep(state.kvK);
                    state.kvV = reorderKvStep(state.kvV);
                    state.kvCk = reorderKvStep(state.kvCk);
                    state.kvCv = reorderKvStep(state.kvCv);
                }
            }

            // 6. select top_k：按 scores 排序，按音素序列去重
            std::vector<std::vector<int64_t>> result(B);
            for (int b = 0; b < B; ++b) {
                std::vector<std::pair<float, int>> cands; // (norm_score, k)
                cands.reserve(K);
                std::unordered_set<std::string> seenPhonemes;
                for (int k = 0; k < K; ++k) {
                    const int idx = b * K + k;
                    const auto &raw = tokensHistory[idx];
                    // 计算归一化 score
                    float normScore = scores[idx];
                    if (lengthPenalty > 0.0f) {
                        int hypLen = static_cast<int>(raw.size());
                        // 找首个 EOS 位置（去除 BOS）
                        for (size_t j = 1; j < raw.size(); ++j) {
                            if (raw[j] == vocab.eosIdx) {
                                hypLen = static_cast<int>(j);
                                break;
                            }
                        }
                        if (hypLen == 0) hypLen = maxLen;
                        normScore /= std::pow(static_cast<float>(hypLen), lengthPenalty);
                    }
                    // 音素序列 key
                    std::string key;
                    for (size_t j = 1; j < raw.size(); ++j) { // 跳过 BOS
                        if (raw[j] == vocab.eosIdx || raw[j] == vocab.padIdx || raw[j] == vocab.unkIdx) continue;
                        key += vocab.phonemeAt(static_cast<int>(raw[j]));
                        key += "\x1f";
                    }
                    if (seenPhonemes.count(key)) continue;
                    seenPhonemes.insert(key);
                    cands.emplace_back(normScore, k);
                }
                std::sort(cands.begin(), cands.end(),
                          [](const auto &a, const auto &b) { return a.first > b.first; });
                const int limit = std::min(topK, static_cast<int>(cands.size()));
                for (int i = 0; i < limit; ++i) {
                    const int k = cands[i].second;
                    const auto &raw = tokensHistory[b * K + k];
                    std::vector<int64_t> cleaned;
                    for (size_t j = 1; j < raw.size(); ++j) { // 跳过 BOS
                        if (raw[j] == vocab.eosIdx || raw[j] == vocab.padIdx || raw[j] == vocab.unkIdx) continue;
                        cleaned.push_back(raw[j]);
                    }
                    result[b].insert(result[b].end(), cleaned.begin(), cleaned.end());
                    // 用 -1 分隔 top_k 候选（greedy 路径只有 1 个）
                    if (limit > 1 && i + 1 < limit) result[b].push_back(-1);
                }
            }
            return DecodeResult{std::move(result)};
        }

        // ============== 音素解码 ==============

        static std::vector<std::string>
        decodePhonemes(const std::vector<int64_t> &ids, const VocabularyData &vocab) {
            std::vector<std::string> phonemes;
            for (const auto id : ids) {
                if (id < 0) continue; // -1 是 top_k 分隔符
                if (id == vocab.bosIdx || id == vocab.eosIdx ||
                    id == vocab.padIdx || id == vocab.unkIdx) {
                    continue;
                }
                phonemes.push_back(vocab.phonemeAt(static_cast<int>(id)));
            }
            return phonemes;
        }

    }

    // ============== Multig2pTaskImpl::start ==============

    srt::core::Expected<srt::core::NO<srt::g2p::TaskResult>>
    Multig2pTaskImpl::start(const srt::core::NO<srt::g2p::TaskInput> &input) {
        if (!input) {
            return srt::g2p::Error(srt::g2p::Error::ConfigError, "g2p input is nullptr");
        }

        const auto g2pInput = input.as<srt::g2p::G2pInputV1>();
        if (!g2pInput) {
            return srt::g2p::Error(srt::g2p::Error::ValidationError,
                                   "type mismatch, expected G2pInputV1");
        }
        if (g2pInput->g2pInput.empty()) {
            return srt::g2p::Error(srt::g2p::Error::ConfigError, "input words are empty");
        }

        // 驱动不可用 → 降级返回原 lyric
        if (!m_driverAvailable) {
            return makeFallbackResult(g2pInput->g2pInput);
        }

        {
            std::shared_lock lock(m_mutex);
            if (!m_driver) {
                return srt::g2p::Error(srt::g2p::Error::RuntimeError, "inference driver not initialized");
            }
        }

        // 1. 构造 langRefs（G2pInputV1 无 languageId 字段，统一用配置默认 lang_ref）
        std::vector<std::string> langRefs(g2pInput->g2pInput.size(), m_defaultLangRef);

        // 2. 批量预处理
        auto preExp = InferenceHelper::preprocessBatch(
            g2pInput->g2pInput, langRefs, m_vocab, m_langIdMap, m_defaultLangRef);
        if (!preExp) {
            return preExp.takeError();
        }
        auto pre = preExp.take();

        // 3. 解码
        srt::core::Expected<InferenceHelper::DecodeResult> decodeExp;
        if (m_beamSize > 1) {
            decodeExp = InferenceHelper::decodeBeam(
                m_encoderSession, m_decoderStepInitSession, m_decoderStepSession,
                pre.src, pre.langIds, pre.srcPadMask,
                m_vocab, m_maxLen, m_beamSize, m_topK, m_lengthPenalty);
        } else {
            decodeExp = InferenceHelper::decodeGreedy(
                m_encoderSession, m_decoderStepInitSession, m_decoderStepSession,
                pre.src, pre.langIds, pre.srcPadMask,
                m_vocab, m_maxLen);
        }
        if (!decodeExp) {
            // 解码失败，按 fallback 处理（保留原 lyric）
            auto g2pResult = srt::core::NO<srt::g2p::G2pResultV1>::create();
            g2pResult->g2pResult.reserve(g2pInput->g2pInput.size());
            for (const auto &lyric : g2pInput->g2pInput) {
                g2pResult->g2pResult.emplace_back(srt::g2p::G2pRes{
                    std::string(lyric), std::string(m_spec->id()), std::string(),
                    stdc::VersionNumber{}, std::string(lyric),
                    std::vector<std::string>(), std::string(srt::g2p::kG2pModeCopy),
                    srt::g2p::ModelInferenceFailed, std::string()});
            }
            return g2pResult;
        }
        auto decodeResult = decodeExp.take();

        // 4. 解码 token → 音素字符串
        auto g2pResult = srt::core::NO<srt::g2p::G2pResultV1>::create();
        g2pResult->g2pResult.reserve(g2pInput->g2pInput.size());

        for (size_t i = 0; i < g2pInput->g2pInput.size(); ++i) {
            const auto &lyric = g2pInput->g2pInput[i];
            auto phonemes = InferenceHelper::decodePhonemes(decodeResult.tokens[i], m_vocab);

            std::string pronStr;
            for (const auto &p : phonemes) {
                pronStr += p + " ";
            }

            if (phonemes.empty()) {
                g2pResult->g2pResult.emplace_back(srt::g2p::G2pRes{
                    std::string(lyric), std::string(m_spec->id()), std::string(),
                    stdc::VersionNumber{}, std::string(lyric),
                    std::vector<std::string>(), std::string(srt::g2p::kG2pModeCopy),
                    srt::g2p::PhonemeGenerationFailed, std::string()});
            } else {
                g2pResult->g2pResult.emplace_back(srt::g2p::G2pRes{
                    std::string(lyric), std::string(m_spec->id()), std::string(),
                    stdc::VersionNumber{}, std::string(pronStr),
                    std::vector<std::string>(), std::string(srt::g2p::kG2pModeConvert)});
            }
        }

        return g2pResult;
    }

}
