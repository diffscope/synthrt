#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <diffsinger/Infer/InferenceResult.h>

namespace dsinfer_cli {

    /// Write inference result audio to a WAV file (IEEE_FLOAT, 32-bit).
    ///
    /// Adapts main.cpp's inline WAV writing block to the session facade's
    /// ds::infer::InferenceResult, which carries float PCM samples plus
    /// sampleRate/channels.
    ///
    /// \return 0 on success, non-zero on error.
    int writeWav(const std::filesystem::path &outputPath,
                 const ds::infer::InferenceResult &result);

} // namespace dsinfer_cli
