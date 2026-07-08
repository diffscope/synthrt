#include "wav_writer.h"

#include "cli_log.h"

#include <cstddef>

#include <WavFile.h>
#include <stdcorelib/path.h>
#include <stdcorelib/str.h>

namespace dsinfer_cli {

// Extracted from main.cpp's execInput WAV writing block (around lines
// 1452-1477). The original code operated on a std::vector<uint8_t> taken
// from the vocoder result with a hardcoded 44100/1/float32 format. The
// session facade returns a ds::infer::InferenceResult carrying float PCM
// samples plus sampleRate/channels, so the format is derived from the
// result (with sane fallbacks to preserve the original 44100/1 defaults).
int writeWav(const std::filesystem::path &outputPath,
             const ds::infer::InferenceResult &result) {
    using ds::infer::WavFile;

    WavFile::DataFormat format{};
    format.container = WavFile::Container::RIFF;
    format.format = WavFile::WaveFormat::IEEE_FLOAT;
    format.channels = static_cast<uint32_t>(result.channels > 0 ? result.channels : 1);
    format.sampleRate =
        static_cast<uint32_t>(result.sampleRate > 0 ? result.sampleRate : 44100);
    format.bitsPerSample = 32;

    WavFile wav;
    if (!wav.init_file_write(outputPath, format)) {
        cliLog.srtCritical("Failed to initialize WAV writer.");
        return -1;
    }

    // result.audio is a vector<float> of interleaved PCM samples (one sample
    // per channel per frame), so frame count = sample count / channel count.
    // (The original main.cpp divided by sizeof(float) because it operated on a
    // byte vector; that factor is not needed for float samples.)
    const auto &audio = result.audio;
    const auto totalPCMFrameCount = audio.size() / static_cast<size_t>(format.channels);

    auto framesWritten = wav.write_pcm_frames(totalPCMFrameCount, audio.data());
    if (framesWritten != totalPCMFrameCount) {
        cliLog.srtCritical("Failed to write all frames.");
    }
    wav.close();

    cliLog.srtSuccess("Saved audio to " + stdc::path::to_utf8(outputPath));
    return 0;
}

} // namespace dsinfer_cli
