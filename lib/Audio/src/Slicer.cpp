#include <synthrt/Audio/Slicer.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <numeric>
#include <utility>

#ifdef SRT_AUDIO_ENABLE_XSIMD
#include <xsimd/xsimd.hpp>
#endif

namespace srt::audio {

    /**
     * @brief Returns the offset from `begin` to the first minimum element
     * @tparam Iterator Forward iterator type
     * @param begin Start of range (inclusive)
     * @param end End of range (exclusive)
     * @return Distance from `begin` to the minimum element; 0 if range is empty
     * @note For equivalent elements, returns the first occurrence.
     *       TD-10: 空区间时显式返回 0（即 std::distance(begin, begin)）作为
     *       最小危害哨兵值，避免 Release 模式下 assert 消失导致的 UB（ROBUST-05）。
     *       调用方（slice 内）不检查返回值，返回 -1 会与 silence_start 相加得到
     *       -1 导致越界；返回 0 得到 silence_start 至少是合法索引。配合 TD-11 对
     *       m_maxSilKept 的非负 clamp，正常流程下调用方传入区间均非空。
     */
    template <typename Iterator>
    static inline int64_t argmin(const Iterator &begin, const Iterator &end) {
        if (begin == end) {
            return 0;
        }
        return std::distance(begin, std::min_element(begin, end));
    }

    static inline std::vector<double> get_rms_impl_basic(const std::vector<float> &samples, const int frame_length,
                                                         const int hop_length) {
        std::vector<double> output;
        // BUG-AUDIO-05: guard against zero/negative parameters which would
        // cause division by zero below (ROBUST-05).
        if (frame_length <= 0 || hop_length <= 0) {
            return output;
        }
        const size_t output_size = samples.size() / hop_length;
        output.reserve(output_size);

        for (size_t i = 0; i < output_size; ++i) {
            const bool is_underflow = i * hop_length < frame_length / 2;
            const size_t start = is_underflow ? 0 : (i * hop_length - frame_length / 2);
            const size_t end = (std::min)(samples.size(), i * hop_length - frame_length / 2 + frame_length);

            const double sum = std::accumulate(samples.begin() + start, samples.begin() + end, 0.0,
                                               [](const double acc, const float value) {
                                                   return acc + value * value;
                                               });
            output.push_back(std::sqrt(sum / frame_length));
        }

        return output;
    }

#ifdef SRT_AUDIO_ENABLE_XSIMD
    static inline double simd_sum(const std::vector<float> &arr, const size_t index_start, const size_t index_end) {
        if (index_start >= index_end) {
            return 0.0;
        }
        double local_sum = 0.0;
        constexpr size_t simd_width = xsimd::batch<float>::size;
        size_t j = index_start;

        // SIMD loop over the range [start, end)
        for (; j + simd_width <= index_end; j += simd_width) {
            // Load the batch of samples into SIMD registers
            xsimd::batch<float> sample_batch = xsimd::load_unaligned(&arr[j]);

            // Square the values
            xsimd::batch<float> squared = sample_batch * sample_batch;

            // Sum the values in the SIMD batch
            local_sum += xsimd::reduce_add(squared);
        }

        // Handle any remaining elements (if any)
        for (; j < index_end; ++j) {
            // Process the remaining scalar values
            local_sum += arr[j] * arr[j];
        }
        return local_sum;
    }

    static inline std::vector<double> get_rms_impl_xsimd(const std::vector<float> &samples, const int frame_length,
                                                         const int hop_length) {
        std::vector<double> output;
        // BUG-AUDIO-05: guard against zero/negative parameters which would
        // cause division by zero below (ROBUST-05).
        if (frame_length <= 0 || hop_length <= 0) {
            return output;
        }
        const size_t output_size = samples.size() / hop_length;
        output.reserve(output_size);

        for (size_t i = 0; i < output_size; ++i) {
            const bool is_underflow = i * hop_length < frame_length / 2;
            const size_t start = is_underflow ? 0 : (i * hop_length - frame_length / 2);
            const size_t end = (std::min)(samples.size(), i * hop_length - frame_length / 2 + frame_length);

            const double sum = simd_sum(samples, start, end);

            output.push_back(std::sqrt(sum / frame_length)); // Calculate RMS for the frame
        }

        return output;
    }
#endif

    // https://github.com/stakira/OpenUtau/blob/master/OpenUtau.Core/Analysis/Some.cs
    Slicer::Slicer(int sampleRate, float threshold, int hopSize, int winSize, int minLength, int minInterval,
                   int maxSilKept) {
        // TD-11: 校验 int 参数非负并 clamp 到合理范围（ROBUST-05）。
        // 负值会导致 slice() 中 size_t 运算下溢、argmin 区间为空（i - m_maxSilKept
        // 越界）等问题。sampleRate/hopSize/winSize 为 0 时 slice() 已有 guard
        // 返回整段；此处仅保证非负，不强制下限 1 以保留 "禁用切片" 的合法配置。
        m_sampleRate = std::max(0, sampleRate);
        m_threshold = threshold;
        m_hopSize = std::max(0, hopSize);
        m_winSize = std::max(0, winSize);
        m_minLength = std::max(0, minLength);
        m_minInterval = std::max(0, minInterval);
        m_maxSilKept = std::max(0, maxSilKept);
    }

    std::vector<double> Slicer::getRms(const std::vector<float> &samples, const int frame_length,
                                       const int hop_length) {
#ifdef SRT_AUDIO_ENABLE_XSIMD
        return get_rms_impl_xsimd(samples, frame_length, hop_length);
#else
        return get_rms_impl_basic(samples, frame_length, hop_length);
#endif
    }

    MarkerList Slicer::slice(const std::vector<float> &samples) const {
        // BUG-AUDIO-05: guard against zero/negative hop/window sizes which
        // would cause division by zero in the RMS computation and the
        // leading `(size + hop - 1) / hop` estimate (ROBUST-05).
        if (m_hopSize <= 0 || m_winSize <= 0) {
            return {{0, static_cast<int64_t>(samples.size())}};
        }
        if ((samples.size() + m_hopSize - 1) / m_hopSize <= m_minLength) {
            return {{0, samples.size()}};
        }

        auto rms_list = getRms(samples, m_winSize, m_hopSize);
        MarkerList sil_tags;
        int64_t silence_start = -1;
        int64_t clip_start = 0;

        for (int64_t i = 0; i < rms_list.size(); ++i) {
            const double rms = rms_list[i];

            if (rms < m_threshold) {
                if (silence_start < 0) {
                    silence_start = i;
                }
                continue;
            }

            if (silence_start < 0) {
                continue;
            }

            bool is_leading_silence = silence_start == 0 && i > m_maxSilKept;
            bool need_slice_middle = i - silence_start >= m_minInterval && i - clip_start >= m_minLength;

            if (!is_leading_silence && !need_slice_middle) {
                silence_start = -1;
                continue;
            }

            if (i - silence_start <= m_maxSilKept) {
                int64_t pos = argmin(rms_list.begin() + silence_start, rms_list.begin() + i + 1);
                pos += silence_start;
                sil_tags.emplace_back((silence_start == 0 ? 0 : pos), pos);
                clip_start = pos;
            } else {
                int64_t pos_l =
                    argmin(rms_list.begin() + silence_start, rms_list.begin() + silence_start + m_maxSilKept + 1);
                int64_t pos_r = argmin(rms_list.begin() + i - m_maxSilKept, rms_list.begin() + i + 1);
                pos_l += silence_start;
                pos_r += i - m_maxSilKept;

                if (silence_start == 0) {
                    sil_tags.emplace_back(0, pos_r);
                } else {
                    sil_tags.emplace_back(pos_l, pos_r);
                }

                clip_start = pos_r;
            }

            silence_start = -1;
        }

        if (silence_start >= 0 && rms_list.size() - silence_start >= m_minInterval) {
            const int64_t silence_end = (std::min)(static_cast<int64_t>(rms_list.size() - 1), silence_start + m_maxSilKept);
            int64_t pos = argmin(rms_list.begin() + silence_start, rms_list.begin() + silence_end + 1);
            pos += silence_start;
            sil_tags.emplace_back(pos, rms_list.size() + 1);
        }

        if (sil_tags.empty()) {
            return {{0, samples.size()}};
        } else {
            MarkerList chunks;

            if (sil_tags[0].first > 0) {
                chunks.emplace_back(0, sil_tags[0].first * m_hopSize);
            }

            for (size_t i = 0; i < sil_tags.size() - 1; ++i) {
                chunks.emplace_back(sil_tags[i].second * m_hopSize, sil_tags[i + 1].first * m_hopSize);
            }

            if (sil_tags.back().second < rms_list.size()) {
                chunks.emplace_back(sil_tags.back().second * m_hopSize, rms_list.size() * m_hopSize);
            }
            return chunks;
        }
    }

} // namespace srt::audio
