#pragma once

#include <filesystem>
#include <vector>

#include "midi_parser.h"

#ifdef DSINFER_CLI_HAS_OPENDSPX
std::vector<MidiNote> parseDspx(const std::filesystem::path &path);
#endif
