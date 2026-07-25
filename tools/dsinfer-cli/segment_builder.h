#pragma once

#include <vector>

#include "note_data.h"

// Build a single continuous MidiPiece from all input notes.
MidiPiece buildContinuousPiece(const std::vector<MidiNote> &input);

// Build continuous segments, each targeting maxDurationSec.
std::vector<MidiPiece> buildContinuousSegments(const std::vector<MidiNote> &input,
                                               double maxDurationSec);
