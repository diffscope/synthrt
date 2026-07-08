#include "segment_builder.h"

#include <algorithm>

// Fill gaps between consecutive notes with SP rest notes so the entire song
// becomes one continuous segment. Notes must be sorted by start tick already.
void fillGapsWithSp(std::vector<MidiNote> &notes) {
    std::vector<MidiNote> result;
    result.reserve(notes.size());
    for (size_t i = 0; i < notes.size(); ++i) {
        result.push_back(notes[i]);
        if (i + 1 >= notes.size()) break;
        const auto &cur = notes[i];
        const auto &next = notes[i + 1];
        const auto nextHeaderStart = next.startMs - headerMinMs(next);
        const auto curTailEnd = cur.endMs + tailMs(cur);
        if (nextHeaderStart > curTailEnd) {
            MidiNote sp;
            sp.lyric = "SP";
            sp.startTick = cur.endTick;
            sp.endTick = next.startTick;
            sp.key = 0;
            sp.startMs = curTailEnd;
            sp.endMs = nextHeaderStart;
            result.push_back(sp);
        }
    }
    notes = std::move(result);
}

// Build a single continuous MidiPiece from all input notes.
// Overlapping notes are filtered out, then gaps are filled with SP rests.
MidiPiece buildContinuousPiece(const std::vector<MidiNote> &input) {
    std::vector<MidiNote> notes;
    for (const auto &note : input) {
        if (note.endTick > note.startTick) notes.push_back(note);
    }
    std::sort(notes.begin(), notes.end(), [](const auto &a, const auto &b) {
        if (a.startTick != b.startTick) return a.startTick < b.startTick;
        return a.key < b.key;
    });

    std::vector<MidiNote> filtered;
    uint32_t lastEnd = 0;
    for (const auto &note : notes) {
        if (!filtered.empty() && note.startTick < lastEnd) continue;
        filtered.push_back(note);
        lastEnd = note.endTick;
    }

    MidiPiece piece;
    piece.notes = std::move(filtered);
    return piece;
}

// Build continuous segments from the input notes.
// Each segment targets maxDurationSec of audio with gaps filled by SP rests.
std::vector<MidiPiece> buildContinuousSegments(const std::vector<MidiNote> &input,
                                               double maxDurationSec) {
    std::vector<MidiNote> notes;
    for (const auto &note : input) {
        if (note.endTick > note.startTick) notes.push_back(note);
    }
    std::sort(notes.begin(), notes.end(), [](const auto &a, const auto &b) {
        if (a.startTick != b.startTick) return a.startTick < b.startTick;
        return a.key < b.key;
    });

    std::vector<MidiNote> filtered;
    uint32_t lastEnd = 0;
    for (const auto &note : notes) {
        if (!filtered.empty() && note.startTick < lastEnd) continue;
        filtered.push_back(note);
        lastEnd = note.endTick;
    }

    std::vector<MidiPiece> pieces;
    std::vector<MidiNote> buffer;
    double segmentStartMs = 0;
    for (size_t i = 0; i < filtered.size();) {
        // Start a new segment
        segmentStartMs = filtered[i].startMs;
        buffer.clear();

        // Gather notes up to maxDurationSec
        while (i < filtered.size()) {
            const auto &cur = filtered[i];
            if (cur.endMs - segmentStartMs > maxDurationSec * 1000.0 && !buffer.empty()) {
                break;
            }
            buffer.push_back(cur);
            ++i;
        }

        if (buffer.empty() && i < filtered.size()) {
            buffer.push_back(filtered[i]);
            ++i;
        }

        if (buffer.empty()) break;

        MidiPiece piece;
        piece.notes = std::move(buffer);
        pieces.push_back(std::move(piece));
    }

    return pieces;
}

std::vector<MidiPiece> segmentMidi(const std::vector<MidiNote> &input) {
    std::vector<MidiNote> notes;
    for (const auto &note : input) {
        if (note.endTick > note.startTick) notes.push_back(note);
    }
    std::sort(notes.begin(), notes.end(), [](const auto &a, const auto &b) {
        if (a.startTick != b.startTick) return a.startTick < b.startTick;
        return a.key < b.key;
    });

    std::vector<MidiNote> filtered;
    uint32_t lastEnd = 0;
    for (const auto &note : notes) {
        if (!filtered.empty() && note.startTick < lastEnd) continue;
        filtered.push_back(note);
        lastEnd = note.endTick;
    }

    std::vector<MidiPiece> pieces;
    std::vector<MidiNote> buffer;
    double previousTailEnd = 0;
    auto commit = [&]() {
        if (buffer.empty()) return;
        if (isSlurOrPlus(buffer.front())) {
            buffer.clear();
            return;
        }
        MidiPiece piece;
        piece.notes = buffer;
        previousTailEnd = piece.notes.back().endMs + tailMs(piece.notes.back());
        pieces.push_back(std::move(piece));
        buffer.clear();
    };

    for (size_t i = 0; i < filtered.size(); ++i) {
        buffer.push_back(filtered[i]);
        if (i + 1 == filtered.size()) {
            commit();
            break;
        }
        const auto &cur = filtered[i];
        const auto &next = filtered[i + 1];
        const auto nextHeaderStart = next.startMs - headerMinMs(next);
        const auto curTailEnd = cur.endMs + tailMs(cur);
        if (nextHeaderStart > curTailEnd) {
            commit();
        }
    }
    (void) previousTailEnd;
    return pieces;
}
