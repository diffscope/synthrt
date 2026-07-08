#include "note_data.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <stdcorelib/str.h>

namespace {
    namespace fs = std::filesystem;

    std::vector<std::string> splitUtf8Chars(const std::string &text) {
        std::vector<std::string> chars;
        for (size_t i = 0; i < text.size();) {
            const auto c = static_cast<unsigned char>(text[i]);
            size_t len = 1;
            if ((c & 0xe0) == 0xc0) len = 2;
            else if ((c & 0xf0) == 0xe0) len = 3;
            else if ((c & 0xf8) == 0xf0) len = 4;
            chars.push_back(text.substr(i, std::min(len, text.size() - i)));
            i += len;
        }
        return chars;
    }

    std::string stripPinyinTone(std::string value) {
        static const std::pair<std::string_view, std::string_view> replacements[] = {
            {"ā", "a"}, {"á", "a"}, {"ǎ", "a"}, {"à", "a"},
            {"ē", "e"}, {"é", "e"}, {"ě", "e"}, {"è", "e"},
            {"ī", "i"}, {"í", "i"}, {"ǐ", "i"}, {"ì", "i"},
            {"ō", "o"}, {"ó", "o"}, {"ǒ", "o"}, {"ò", "o"},
            {"ū", "u"}, {"ú", "u"}, {"ǔ", "u"}, {"ù", "u"},
            {"ǖ", "v"}, {"ǘ", "v"}, {"ǚ", "v"}, {"ǜ", "v"}, {"ü", "v"},
        };
        for (const auto &[from, to] : replacements) {
            size_t pos = 0;
            while ((pos = value.find(from, pos)) != std::string::npos) {
                value.replace(pos, from.size(), to);
                pos += to.size();
            }
        }
        return value;
    }

    const std::map<std::string, std::string> &mandarinWordDict() {
        static const auto dict = [] {
            std::map<std::string, std::string> result;
#ifdef DSINFER_CLI_G2P_DICT_DIR
            const auto path = fs::path(DSINFER_CLI_G2P_DICT_DIR) / "mandarin" / "word.txt";
            std::ifstream file(path);
            std::string line;
            while (std::getline(file, line)) {
                const auto sep = line.find(':');
                if (sep == std::string::npos) continue;
                auto key = line.substr(0, sep);
                auto value = line.substr(sep + 1);
                const auto comma = value.find(',');
                if (comma != std::string::npos) value.resize(comma);
                result.emplace(std::move(key), stripPinyinTone(std::move(value)));
            }
#endif
            return result;
        }();
        return dict;
    }
} // namespace

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

std::string lyricToPronunciation(const std::string &lyric, const std::string &languageId) {
    if (languageId != "cmn") {
        return lyric;
    }
    const auto &dict = mandarinWordDict();
    std::vector<std::string> syllables;
    for (const auto &ch : splitUtf8Chars(lyric)) {
        if (auto it = dict.find(ch); it != dict.end()) {
            syllables.push_back(it->second);
        } else {
            syllables.push_back(ch);
        }
    }
    return stdc::join(syllables, " ");
}
