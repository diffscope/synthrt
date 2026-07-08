#include "midi_parser.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <stdexcept>

#include <stdcorelib/path.h>

namespace fs = std::filesystem;

namespace {
    struct MidiReader {
        std::vector<uint8_t> data;
        size_t pos = 0;

        explicit MidiReader(std::vector<uint8_t> bytes) : data(std::move(bytes)) {}

        uint8_t u8() {
            if (pos >= data.size()) throw std::runtime_error("unexpected end of MIDI data");
            return data[pos++];
        }

        uint16_t be16() {
            const auto a = u8();
            const auto b = u8();
            return static_cast<uint16_t>((a << 8) | b);
        }

        uint32_t be32() {
            const auto a = u8();
            const auto b = u8();
            const auto c = u8();
            const auto d = u8();
            return (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(b) << 16) |
                   (static_cast<uint32_t>(c) << 8) | static_cast<uint32_t>(d);
        }

        uint32_t vlq() {
            uint32_t value = 0;
            for (int i = 0; i < 4; ++i) {
                const auto b = u8();
                value = (value << 7) | (b & 0x7f);
                if ((b & 0x80) == 0) return value;
            }
            throw std::runtime_error("invalid MIDI variable-length quantity");
        }

        std::string text(size_t n) {
            if (pos + n > data.size()) throw std::runtime_error("unexpected end of text event");
            std::string s(reinterpret_cast<const char *>(data.data() + pos), n);
            pos += n;
            return s;
        }

        void skip(size_t n) {
            if (pos + n > data.size()) throw std::runtime_error("unexpected end of MIDI event");
            pos += n;
        }
    };

    std::vector<uint8_t> readBinaryFile(const fs::path &path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) throw std::runtime_error("failed to open MIDI file: " + stdc::path::to_utf8(path));
        return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }
}

std::vector<MidiNote> parseMidi(const fs::path &path) {
    MidiReader r(readBinaryFile(path));
    if (r.text(4) != "MThd") throw std::runtime_error("missing MThd header");
    const auto headerLen = r.be32();
    const auto format = r.be16();
    const auto tracks = r.be16();
    const auto division = r.be16();
    if (division & 0x8000) throw std::runtime_error("SMPTE time division is not supported");
    if (headerLen > 6) r.skip(headerLen - 6);
    (void) format;

    std::vector<MidiNote> notes;
    struct TempoEvent {
        uint32_t tick = 0;
        uint32_t usPerQuarter = 500000;
    };
    std::vector<TempoEvent> tempoEvents{{0, 500000}};
    std::multimap<uint32_t, std::string> lyrics;
    struct ActiveNote { uint32_t tick; int key; std::string lyric; };

    for (uint16_t t = 0; t < tracks; ++t) {
        if (r.text(4) != "MTrk") throw std::runtime_error("missing MTrk chunk");
        const auto trackLen = r.be32();
        const auto trackEnd = r.pos + trackLen;
        uint32_t tick = 0;
        uint8_t runningStatus = 0;
        std::map<int, ActiveNote> active;
        while (r.pos < trackEnd) {
            tick += r.vlq();
            auto status = r.u8();
            if (status < 0x80) {
                if (!runningStatus) throw std::runtime_error("running status without status byte");
                --r.pos;
                status = runningStatus;
            } else if (status < 0xf0) {
                runningStatus = status;
            }

            if (status == 0xff) {
                const auto type = r.u8();
                const auto len = r.vlq();
                if (type == 0x51 && len == 3) {
                    tempoEvents.push_back({tick, (static_cast<uint32_t>(r.u8()) << 16) |
                                                    (static_cast<uint32_t>(r.u8()) << 8) | r.u8()});
                } else if (type == 0x01 || type == 0x05) {
                    auto text = r.text(len);
                    if (!text.empty()) lyrics.emplace(tick, std::move(text));
                } else {
                    r.skip(len);
                }
                continue;
            }
            if (status == 0xf0 || status == 0xf7) {
                r.skip(r.vlq());
                continue;
            }

            const auto event = status & 0xf0;
            const auto data1 = r.u8();
            const bool twoData = event != 0xc0 && event != 0xd0;
            const auto data2 = twoData ? r.u8() : 0;
            if (event == 0x90 && data2 > 0) {
                std::string lyric = "la";
                // Find most recent lyric text event at or before this tick
                auto it = lyrics.upper_bound(tick);
                if (it != lyrics.begin()) {
                    --it;
                    lyric = it->second;
                    lyrics.erase(it);
                }
                active[data1] = ActiveNote{tick, data1, lyric};
            } else if (event == 0x80 || (event == 0x90 && data2 == 0)) {
                auto it = active.find(data1);
                if (it != active.end()) {
                    MidiNote note;
                    note.startTick = it->second.tick;
                    note.endTick = tick;
                    note.key = it->second.key;
                    note.lyric = it->second.lyric;
                    notes.push_back(std::move(note));
                    active.erase(it);
                }
            }
        }
        r.pos = trackEnd;
    }

    std::sort(tempoEvents.begin(), tempoEvents.end(), [](const auto &a, const auto &b) {
        return a.tick < b.tick;
    });

    auto tickToMs = [&](uint32_t targetTick) {
        double ms = 0.0;
        uint32_t prevTick = 0;
        uint32_t tempoUsPerQuarter = 500000;
        for (const auto &event : tempoEvents) {
            if (event.tick > targetTick) {
                break;
            }
            if (event.tick > prevTick) {
                ms += static_cast<double>(event.tick - prevTick) *
                      static_cast<double>(tempoUsPerQuarter) / 1000.0 / division;
                prevTick = event.tick;
            }
            tempoUsPerQuarter = event.usPerQuarter;
        }
        if (targetTick > prevTick) {
            ms += static_cast<double>(targetTick - prevTick) *
                  static_cast<double>(tempoUsPerQuarter) / 1000.0 / division;
        }
        return ms;
    };
    for (auto &note : notes) {
        note.startMs = tickToMs(note.startTick);
        note.endMs = tickToMs(note.endTick);
    }
    return notes;
}
