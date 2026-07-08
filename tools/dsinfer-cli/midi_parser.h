#pragma once

#include <filesystem>
#include <vector>

#include "note_data.h"

std::vector<MidiNote> parseMidi(const std::filesystem::path &path);
