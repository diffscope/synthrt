#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <diffsinger/Infer/dsinfer/Api/Inferences/Acoustic/1/AcousticApiL1.h>
#include <diffsinger/Bank/SingerRef.h>
#include <synthrt/G2P/LanguageService.h>

#include "midi_parser.h"
#include "note_data.h"

namespace dsinfer_cli {

    /// Run G2P + S2P + buildWords for a MIDI piece.
    ///
    /// Uses the LanguageService component API (resolveLanguageRoute() +
    /// convertLyric()) together with srt::s2p::LanguageResource for
    /// pronunciation -> phoneme splitting.
    InputObject buildInputFromPiece(ds::lang::LanguageService &langSvc,
                                    const ds::bank::SingerRef &ref,
                                    const MidiPiece &piece,
                                    const std::string &speakerId,
                                    const std::string &languageId,
                                    const std::filesystem::path &dumpDataDir = {});

} // namespace dsinfer_cli
