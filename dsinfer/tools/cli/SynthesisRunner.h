#ifndef DSINFER_TOOLS_CLI_SYNTHESISRUNNER_H
#define DSINFER_TOOLS_CLI_SYNTHESISRUNNER_H

#include <filesystem>

#include <synthrt/Core/SynthUnit.h>

#include "SynthesisInput.h"

namespace ds::cli {

    /// Executes one DiffSinger synthesis request on a configured SynthUnit.
    class SynthesisRunner {
    public:
        explicit SynthesisRunner(srt::SynthUnit &synthUnit) : m_synthUnit(synthUnit) {
        }

        /// Runs \a input from \a packagePath and writes a WAV file to \a outputPath.
        void run(const std::filesystem::path &packagePath, SynthesisInput input,
                 const std::filesystem::path &outputPath);

    private:
        srt::SynthUnit &m_synthUnit;
    };

}

#endif // DSINFER_TOOLS_CLI_SYNTHESISRUNNER_H
