#include "note_data.h"

#include <string>

bool isRest(const MidiNote &note) {
    return note.lyric == "SP" || note.lyric == "AP";
}

bool isSlurOrPlus(const MidiNote &note) {
    if (note.lyric == "-") return true;
    return !note.lyric.empty() && note.lyric.find_first_not_of('+') == std::string::npos;
}

double headerMinMs(const MidiNote &note) {
    return isRest(note) ? 0.0 : 100.0;
}

double tailMs(const MidiNote &note) {
    return isRest(note) ? 0.0 : 100.0;
}
