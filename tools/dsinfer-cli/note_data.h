#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <synthrt/Core/Core/NamedObject.h>
#include <diffsinger/Infer/dsinfer/Api/Inferences/Acoustic/1/AcousticApiL1.h>

struct MidiNote {
    uint32_t startTick = 0;
    uint32_t endTick = 0;
    int key = 0;
    std::string lyric;
    std::string language;
    double startMs = 0;
    double endMs = 0;
};

struct MidiPiece {
    std::vector<MidiNote> notes;
};

struct PhEntry {
    std::string token;
    std::string language;
    bool isOnset = false;
};

struct NoteData {
    int startTick = 0;
    int lengthTick = 0;
    double startMs = 0;
    double endMs = 0;
    int key = 0;
    bool isRest = false;
    bool isSlur = false;
    std::string language;
    std::vector<PhEntry> phs;
};

struct InputObject {
    std::string singer;
    srt::core::NO<srt::svs::Api::Acoustic::L1::AcousticStartInput> input;
    std::vector<NoteData> notes;
};

// Helper predicates
bool isRest(const MidiNote &note);
bool isSlurOrPlus(const MidiNote &note);
double headerMinMs(const MidiNote &note);
double tailMs(const MidiNote &note);
