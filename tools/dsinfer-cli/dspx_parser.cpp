#ifdef DSINFER_CLI_HAS_OPENDSPX

#include "dspx_parser.h"

#include <algorithm>
#include <fstream>
#include <stdexcept>

#include <opendspx/model.h>
#include <opendspx/singingclip.h>
#include <opendspxserializer/serializer.h>

#include <stdcorelib/path.h>

namespace fs = std::filesystem;

static double dspxTickToMs(int tick, const std::vector<opendspx::Tempo> &tempos) {
    auto sorted = tempos;
    std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) {
        return a.pos < b.pos;
    });
    if (sorted.empty()) {
        sorted.push_back({});
    }

    double ms = 0.0;
    int cursor = 0;
    double bpm = sorted.front().value;
    for (const auto &tempo : sorted) {
        if (tempo.pos <= cursor) {
            bpm = tempo.value;
            continue;
        }
        if (tempo.pos >= tick) break;
        ms += static_cast<double>(tempo.pos - cursor) * 60000.0 / bpm / 480.0;
        cursor = tempo.pos;
        bpm = tempo.value;
    }
    ms += static_cast<double>(tick - cursor) * 60000.0 / bpm / 480.0;
    return ms;
}

std::vector<MidiNote> parseDspx(const fs::path &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("failed to open DSPX file: " + stdc::path::to_utf8(path));

    opendspx::SerializationErrorList errors;
    auto model = opendspx::Serializer::deserialize(file, errors);
    if (!errors.empty()) {
        throw std::runtime_error("failed to parse DSPX file");
    }

    std::vector<MidiNote> notes;
    for (const auto &track : model.content.tracks) {
        for (const auto &clipRef : track.clips) {
            if (clipRef->type != opendspx::Clip::Type::Singing) continue;
            const auto &sClip = *std::static_pointer_cast<opendspx::SingingClip>(clipRef);
            for (const auto &note : sClip.notes) {
                MidiNote out;
                out.startTick = static_cast<uint32_t>(sClip.time.pos + note.pos - sClip.time.clipStart);
                out.endTick = out.startTick + static_cast<uint32_t>(note.length);
                out.key = note.keyNum;
                out.lyric = note.lyric;
                out.language = note.language;
                out.startMs = dspxTickToMs(static_cast<int>(out.startTick), model.content.timeline.tempos);
                out.endMs = dspxTickToMs(static_cast<int>(out.endTick), model.content.timeline.tempos);
                notes.push_back(std::move(out));
            }
        }
    }
    return notes;
}

#endif // DSINFER_CLI_HAS_OPENDSPX
