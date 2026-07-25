#include "wav_writer.h"

#include "cli_log.h"

#include <cassert>
#include <cstddef>

#include <synthrt/Audio/AudioBuffer.h>
#include <synthrt/Audio/AudioFileWriter.h>
#include <stdcorelib/path.h>
#include <stdcorelib/str.h>

namespace dsinfer_cli {

// Extracted from main.cpp's execInput WAV writing block. Originally built on
// ds::infer::WavFile (dr_wav); rewritten (P4-4) to use srt::audio::AudioFileWriter
// (libsndfile backend) so the dsinfer-cli tool no longer pulls in dr_wav.
//
// The session facade returns a ds::infer::InferenceResult carrying float PCM
// samples plus sampleRate/channels, so the format is derived from the result
// (with sane fallbacks to preserve the original 44100/1 defaults).
int writeWav(const std::filesystem::path &outputPath,
             const ds::infer::InferenceResult &result) {
    const int channels = result.channels > 0 ? result.channels : 1;
    const int sampleRate = result.sampleRate > 0 ? result.sampleRate : 44100;

    srt::audio::AudioFileWriter writer;
    auto openExp = writer.open(stdc::path::to_utf8(outputPath), sampleRate, channels,
                                srt::audio::SampleFormat::Float32);
    if (!openExp) {
        cliLog.srtCritical("Failed to initialize WAV writer: " + openExp.error().message());
        return -1;
    }

    // result.audio is a vector<float> of interleaved PCM samples (one sample
    // per channel per frame), so frame count = sample count / channel count.
    const auto &audio = result.audio;
    // WW-1: 防御性断言——若 audio.size() 不能被 channels 整除，整数除法会
    // 静默丢弃尾部样本。channels 已保证 >= 1，audio 为空时 0 % n == 0 成立。
    // Debug 构建中尽早暴露上游交错数据不完整的问题（防御性编程）。
    assert(audio.size() % static_cast<size_t>(channels) == 0);
    const auto totalPCMFrameCount =
        static_cast<int64_t>(audio.size() / static_cast<size_t>(channels));

    if (totalPCMFrameCount > 0) {
        // AudioBuffer::fromView 创建零拷贝 view，引用 result.audio 的内存。
        // writer.write() 在同一线程内同步消费，期间 result 不会被释放。
        auto buffer = srt::audio::AudioBuffer::fromView(
            audio.data(), totalPCMFrameCount, channels,
            srt::audio::SampleFormat::Float32);

        auto writeExp = writer.write(buffer, sampleRate);
        if (!writeExp) {
            cliLog.srtCritical("Failed to write audio: " + writeExp.error().message());
            // ROBUST-05: 检查 close() 返回值。write 已失败，close 仅为清理；
            // 其失败用 warning 记录，不覆盖原始 write 错误。
            if (auto closeExp = writer.close(); !closeExp) {
                cliLog.srtWarning("Failed to close WAV writer after write error: " +
                                  closeExp.error().message());
            }
            return -1;
        }
    }

    auto closeExp = writer.close();
    if (!closeExp) {
        cliLog.srtCritical("Failed to close WAV writer: " + closeExp.error().message());
        return -1;
    }

    cliLog.srtSuccess("Saved audio to " + stdc::path::to_utf8(outputPath));
    return 0;
}

} // namespace dsinfer_cli
