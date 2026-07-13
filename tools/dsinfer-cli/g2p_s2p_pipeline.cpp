#include "g2p_s2p_pipeline.h"

#include <cctype>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <synthrt/S2P/LanguageResource.h>
#include <synthrt/Core/Support/JSON.h>
#include <synthrt/G2P/LanguageRoute.h>
#include <synthrt/G2P/Base/LangCommon.h>
#include <stdcorelib/path.h>
#include <stdcorelib/str.h>
#include <stdcorelib/support/versionnumber.h>

#include "cli_log.h"

namespace Co = srt::svs::Api::Common::L1;
namespace Ac = srt::svs::Api::Acoustic::L1;

namespace dsinfer_cli {

// Returns true if the lyric is punctuation or a digit — legitimate copy
// fallback cases where pronunciation == lyric is expected (P1-1).
static bool isPunctuationOrDigit(const std::string &lyric) {
    if (lyric.empty())
        return false;
    for (unsigned char ch : lyric) {
        if (std::isalnum(ch) && !std::isdigit(ch))
            return false;  // letter → not punctuation/digit
        if (!std::isdigit(ch) && !std::ispunct(ch) && !std::isspace(ch))
            return false;  // non-ASCII or other → not punctuation/digit
    }
    // At least one character must be a digit or punctuation
    for (unsigned char ch : lyric) {
        if (std::isdigit(ch) || std::ispunct(ch))
            return true;
    }
    return false;  // only whitespace
}

// Refactored from main.cpp's inputFromMidiPiece (lines 724-994).
//
// The buildWords algorithm (lead-in / commit / slur / gap) is preserved
// verbatim. Only the G2P/S2P API surface changes:
//   * LanguageService::resolveG2pRoute() + LanguageService::resolve() are
//     collapsed into a single session.resolveLanguageRoute() call that returns
//     a LanguageRoute carrying both G2P routing and S2P resource references.
//   * srt::g2p::Manager::instance()->convert() is replaced by
//     session.convertLyric().
//   * g2pContext / g2pContextVersion come directly from the LanguageRoute
//     (g2pContextVersion is already a VersionNumber, no string parsing).
InputObject buildInputFromPiece(srt::g2p::LanguageService &langSvc,
                                const ds::bank::SingerRef &ref,
                                const MidiPiece &piece,
                                const std::string &speakerId,
                                const std::string &languageId,
                                const std::filesystem::path &dumpDataDir) {
    if (piece.notes.empty()) {
        throw std::runtime_error("cannot build inference input from an empty MIDI segment");
    }

    // Resolve G2P route + S2P resource via the session facade.
    std::string g2pId;
    std::string g2pContext;
    stdc::VersionNumber g2pContextVersion;
    std::string s2pMode;
    std::filesystem::path s2pFile;
    std::filesystem::path onsetFile;

    auto routeExp = langSvc.resolveLanguageRoute(ref.packageId, ref.singerId, languageId);
    if (routeExp) {
        const auto &route = *routeExp;
        g2pId = route.g2pId;
        // g2pContext is "" (= kOfficialContext) for official G2P and the
        // singerId for voicebank private G2P (R7); g2pSource distinguishes them.
        if (route.g2pSource == srt::g2p::kG2pSourceVoicebank) {
            g2pContext = route.g2pContext;
            g2pContextVersion = route.g2pContextVersion;
        }
        s2pMode = route.s2pMode;
        s2pFile = route.s2pFile;
        onsetFile = route.onsetFile;
    }

    // Build the S2P resource from the route fields.
    // S2P resource construction failure is a hard error (P0-2).
    std::unique_ptr<srt::s2p::LanguageResource> langResource;
    if (!s2pMode.empty()) {
        try {
            if (s2pMode == "dict" && !s2pFile.empty()) {
                langResource = std::unique_ptr<srt::s2p::LanguageResource>(
                    new srt::s2p::LanguageResource(srt::s2p::LanguageResource::dictionary(
                        stdc::path::to_utf8(s2pFile), stdc::path::to_utf8(onsetFile))));
            } else {
                langResource = std::unique_ptr<srt::s2p::LanguageResource>(
                    new srt::s2p::LanguageResource(
                        srt::s2p::LanguageResource::direct(stdc::path::to_utf8(onsetFile))));
            }
        } catch (const std::exception &e) {
            throw std::runtime_error(stdc::formatN(
                "S2P resource construction failed (s2pMode=%1, s2pFile=%2): %3",
                s2pMode, stdc::path::to_utf8(s2pFile), e.what()));
        }
    }

    InputObject result;
    result.singer = ref.singerId;
    result.input = srt::core::NO<Ac::AcousticStartInput>::create();
    result.input->duration = (piece.notes.back().endMs - piece.notes.front().startMs) / 1000.0;
    result.input->steps = 10;
    result.input->speakers.push_back({speakerId, 0.0, {1.0}});

    const bool dumpEnabled = !dumpDataDir.empty();
    srt::core::JsonArray dumpG2pInputs, dumpG2pOutputs, dumpS2pResults;

    std::vector<NoteData> noteData;
    noteData.reserve(piece.notes.size());
    for (const auto &note : piece.notes) {
        NoteData nd;
        nd.startTick = note.startTick;
        nd.lengthTick = note.endTick - note.startTick;
        nd.startMs = note.startMs;
        nd.endMs = note.endMs;
        nd.key = note.key;
        nd.isRest = isRest(note);
        nd.isSlur = isSlurOrPlus(note);
        nd.language = languageId.empty() ? note.language : languageId;

        if (!nd.isRest && !nd.isSlur) {
            const auto &effectiveG2pId = g2pId.empty() ? nd.language : g2pId;

            // --- G2P hard failure (P0-1) ---
            logG2pInput(note.lyric, effectiveG2pId, g2pContext, g2pContextVersion);
            srt::g2p::G2pInput g2pInput(note.lyric, effectiveG2pId, g2pContext, g2pContextVersion);
            auto g2pResults = langSvc.convertLyric({g2pInput});

            if (dumpEnabled) {
                srt::core::JsonObject in;
                in["lyric"] = note.lyric;
                in["g2pId"] = effectiveG2pId;
                in["context"] = g2pContext;
                in["version"] = g2pContextVersion.toString();
                dumpG2pInputs.emplace_back(std::move(in));
            }

            if (g2pResults.empty()) {
                throw std::runtime_error(stdc::formatN(
                    "G2P returned empty results for lyric '%1' (g2pId=%2)",
                    note.lyric, effectiveG2pId));
            }
            const auto &g2pRes = g2pResults[0];
            logG2pOutput(g2pRes.pronunciation, g2pRes.mode, g2pRes.isFailed());

            if (dumpEnabled) {
                srt::core::JsonObject out;
                out["lyric"] = g2pRes.lyric;
                out["pronunciation"] = g2pRes.pronunciation;
                out["mode"] = g2pRes.mode;
                out["failed"] = g2pRes.isFailed();
                out["errorType"] = static_cast<int>(g2pRes.errorType);
                dumpG2pOutputs.emplace_back(std::move(out));
            }

            if (g2pRes.isFailed()) {
                throw std::runtime_error(stdc::formatN(
                    "G2P inference failed for lyric '%1' (errorType=%2)",
                    note.lyric, static_cast<int>(g2pRes.errorType)));
            }
            if (g2pRes.pronunciation.empty()) {
                throw std::runtime_error(stdc::formatN(
                    "G2P returned empty pronunciation for lyric '%1'", note.lyric));
            }
            // Detect copy fallback (pronunciation == lyric means no real conversion).
            // Punctuation/digits are legitimate copy fallbacks (P1-1).
            if (g2pRes.mode == srt::g2p::kG2pModeCopy && g2pRes.pronunciation == note.lyric &&
                !isPunctuationOrDigit(note.lyric)) {
                throw std::runtime_error(stdc::formatN(
                    "G2P returned copy fallback for lyric '%1' (context not Ready, g2pId=%2)",
                    note.lyric, effectiveG2pId));
            }

            std::string pronunciationText = g2pRes.pronunciation;

            // --- S2P hard failure (P0-2) ---
            if (!langResource) {
                throw std::runtime_error(stdc::formatN(
                    "S2P resource not available (s2pMode='%1', s2pFile='%2')",
                    s2pMode, stdc::path::to_utf8(s2pFile)));
            }

            logS2pInput(pronunciationText, s2pMode, s2pFile);
            srt::s2p::SyllablePronunciation pron;
            try {
                pron = langResource->convert(pronunciationText);
            } catch (const std::exception &e) {
                throw std::runtime_error(stdc::formatN(
                    "S2P conversion failed for pronunciation '%1': %2",
                    pronunciationText, e.what()));
            }
            if (pron.phonemes.empty()) {
                throw std::runtime_error(stdc::formatN(
                    "S2P returned empty phonemes for pronunciation '%1'",
                    pronunciationText));
            }
            logS2pOutput(pron.phonemes, pron.onsets);

            if (dumpEnabled) {
                srt::core::JsonObject s2p;
                s2p["pronunciation"] = pronunciationText;
                srt::core::JsonArray phArr;
                for (const auto &ph : pron.phonemes)
                    phArr.emplace_back(ph);
                s2p["phonemes"] = std::move(phArr);
                srt::core::JsonArray onArr;
                for (bool o : pron.onsets)
                    onArr.emplace_back(o);
                s2p["onsets"] = std::move(onArr);
                dumpS2pResults.emplace_back(std::move(s2p));
            }

            for (size_t i = 0; i < pron.phonemes.size(); ++i) {
                bool onset = (i < pron.onsets.size()) ? pron.onsets[i] : (i == 0);
                nd.phs.push_back({pron.phonemes[i], nd.language, onset});
            }
        }
        noteData.push_back(nd);
    }
    result.notes = noteData;

    // Write dump files 03-06 (G2P inputs/outputs, S2P results, note data).
    if (dumpEnabled) {
        std::error_code ec;
        std::filesystem::create_directories(dumpDataDir, ec);
        auto writeFile = [&](const char *name, const srt::core::JsonValue &val) {
            std::ofstream f(dumpDataDir / name, std::ios::binary);
            if (f.is_open())
                f << val.toJson(2);
        };
        writeFile("03_g2p_inputs.json", dumpG2pInputs);
        writeFile("04_g2p_outputs.json", dumpG2pOutputs);
        writeFile("05_s2p_results.json", dumpS2pResults);

        srt::core::JsonArray ndArr;
        for (const auto &nd : noteData) {
            srt::core::JsonObject ndObj;
            ndObj["startTick"] = nd.startTick;
            ndObj["lengthTick"] = nd.lengthTick;
            ndObj["startMs"] = nd.startMs;
            ndObj["endMs"] = nd.endMs;
            ndObj["key"] = nd.key;
            ndObj["isRest"] = nd.isRest;
            ndObj["isSlur"] = nd.isSlur;
            ndObj["language"] = nd.language;
            srt::core::JsonArray phArr;
            for (const auto &ph : nd.phs) {
                srt::core::JsonObject phObj;
                phObj["token"] = ph.token;
                phObj["language"] = ph.language;
                phObj["isOnset"] = ph.isOnset;
                phArr.emplace_back(std::move(phObj));
            }
            ndObj["phs"] = std::move(phArr);
            ndArr.emplace_back(std::move(ndObj));
        }
        writeFile("06_note_data.json", ndArr);
    }

    // -- Second pass: build words following ds-editor-lite's buildWords algorithm --
    // Key semantics: commit() does nothing when phoneBuffer is empty, which lets
    // rest/slur durations (notes without phonemes) merge into the next word.
    constexpr double paddingSec = 0.1;

    std::vector<Co::InputNoteInfo> noteBuffer;
    std::vector<Co::InputPhonemeInfo> phoneBuffer;

    auto commit = [&] {
        if (phoneBuffer.empty())
            return;
        // First phone of a word is the onset — its start is anchored to 0.
        phoneBuffer.front().start = 0;
        Co::InputWordInfo w;
        w.notes = std::move(noteBuffer);
        w.phones = std::move(phoneBuffer);
        result.input->words.push_back(std::move(w));
        noteBuffer.clear();
        phoneBuffer.clear();
    };

    auto addSpeaker = [&](Co::InputPhonemeInfo &phone) {
        phone.speakers.push_back({speakerId, 1.0});
    };

    // Lead-in word: only if first note is NOT a rest (matches ds-editor-lite).
    if (!noteData.front().isRest) {
        const auto &first = noteData.front();

        Co::InputNoteInfo ln;
        ln.key = 0;
        ln.duration = paddingSec;
        ln.is_rest = true;
        noteBuffer.push_back(ln);

        Co::InputPhonemeInfo sp;
        sp.token = "SP";
        sp.language = first.language;
        sp.start = 0;
        addSpeaker(sp);
        phoneBuffer.push_back(sp);

        // Pre-onset phonemes of the first note (if any) belong to the lead-in word.
        for (const auto &ph : first.phs) {
            if (ph.isOnset) break;
            Co::InputPhonemeInfo phone;
            phone.token = ph.token;
            phone.language = ph.language;
            phone.start = 0;
            addSpeaker(phone);
            phoneBuffer.push_back(std::move(phone));
        }
        commit();
    }

    // Main loop: process ALL notes in order (including rests and slurs).
    // Rests have empty phs, so commit() becomes a no-op and their durations
    // accumulate in noteBuffer to be merged into the next word.
    size_t noteIndex = 0;
    int lastKey = 0;
    while (noteIndex < noteData.size()) {
        const auto &nd = noteData[noteIndex];
        lastKey = nd.key;

        const double noteStartMs = nd.startMs;
        const double noteEndMs = nd.endMs;
        const double noteDurSec = (noteEndMs - noteStartMs) / 1000.0;

        Co::InputNoteInfo in;
        in.key = nd.key;
        in.duration = noteDurSec;
        in.is_rest = nd.isRest;
        noteBuffer.push_back(in);

        // Add onset+ phonemes of the current note (pre-onset phonemes were
        // either appended to the previous word or stashed into the gap word).
        bool onsetReached = false;
        for (const auto &ph : nd.phs) {
            if (!ph.isOnset && !onsetReached) continue;
            onsetReached = true;
            Co::InputPhonemeInfo phone;
            phone.token = ph.token;
            phone.language = ph.language;
            phone.start = 0;
            addSpeaker(phone);
            phoneBuffer.push_back(std::move(phone));
        }

        // Consume slur notes that immediately follow — they extend the current
        // word's note list without adding any phonemes.
        double curEndMs = noteEndMs;
        while (noteIndex + 1 < noteData.size() && noteData[noteIndex + 1].isSlur) {
            const auto &slur = noteData[noteIndex + 1];
            const double slurStartMs = slur.startMs;
            const double slurEndMs = slur.endMs;
            Co::InputNoteInfo sn;
            sn.key = slur.key;
            sn.duration = (slurEndMs - slurStartMs) / 1000.0;
            sn.is_rest = false;
            noteBuffer.push_back(sn);
            curEndMs = slurEndMs;
            ++noteIndex;
        }

        // Look ahead to the next non-slur note to compute the gap and to splice
        // its pre-onset phonemes (if any) into the current word or the gap word.
        double gapSec = 0;
        bool hasGap = false;
        std::vector<Co::InputPhonemeInfo> stashedNextPhones;
        if (noteIndex + 1 < noteData.size()) {
            const auto &nextNd = noteData[noteIndex + 1];
            const double nextStartMs = nextNd.startMs;
            gapSec = (nextStartMs - curEndMs) / 1000.0;
            hasGap = gapSec > 1e-4;

            for (const auto &ph : nextNd.phs) {
                if (ph.isOnset) break;
                Co::InputPhonemeInfo phone;
                phone.token = ph.token;
                phone.language = ph.language;
                phone.start = 0;
                addSpeaker(phone);
                if (!hasGap) {
                    // No gap: pre-onset phonemes stay in the current word.
                    phoneBuffer.push_back(std::move(phone));
                } else {
                    // Has gap: pre-onset phonemes go into the gap word.
                    stashedNextPhones.push_back(std::move(phone));
                }
            }
        }

        commit();

        // If there is a gap, commit a gap word (SP + stashed pre-onset phonemes).
        // The gap word comes AFTER the current word — this preserves time order.
        if (hasGap) {
            Co::InputNoteInfo gn;
            gn.key = lastKey;
            gn.duration = gapSec;
            gn.is_rest = true;
            noteBuffer.push_back(gn);

            Co::InputPhonemeInfo gsp;
            gsp.token = "SP";
            gsp.language = nd.language;
            gsp.start = 0;
            addSpeaker(gsp);
            phoneBuffer.push_back(gsp);

            for (auto &phone : stashedNextPhones) {
                phoneBuffer.push_back(std::move(phone));
            }
            commit();
        }

        ++noteIndex;
    }

    // Tail word: only if last note is NOT a rest (matches ds-editor-lite).
    if (!noteData.back().isRest) {
        Co::InputNoteInfo tn;
        tn.key = lastKey;
        tn.duration = paddingSec;
        tn.is_rest = true;
        noteBuffer.push_back(tn);

        Co::InputPhonemeInfo tsp;
        tsp.token = "SP";
        tsp.language = noteData.front().language;
        tsp.start = 0;
        addSpeaker(tsp);
        phoneBuffer.push_back(tsp);
        commit();
    }

    logBuildWordsSummary(result.input->words.size(), noteData.size());

    // Write dump file 07_words.json.
    if (dumpEnabled) {
        srt::core::JsonArray wordsArr;
        for (const auto &w : result.input->words) {
            srt::core::JsonObject wObj;
            srt::core::JsonArray notesArr;
            for (const auto &n : w.notes) {
                srt::core::JsonObject nObj;
                nObj["key"] = n.key;
                nObj["duration"] = n.duration;
                nObj["is_rest"] = n.is_rest;
                notesArr.emplace_back(std::move(nObj));
            }
            wObj["notes"] = std::move(notesArr);
            srt::core::JsonArray phonesArr;
            for (const auto &p : w.phones) {
                srt::core::JsonObject pObj;
                pObj["token"] = p.token;
                pObj["language"] = p.language;
                pObj["start"] = p.start;
                phonesArr.emplace_back(std::move(pObj));
            }
            wObj["phones"] = std::move(phonesArr);
            wordsArr.emplace_back(std::move(wObj));
        }
        std::ofstream f(dumpDataDir / "07_words.json", std::ios::binary);
        if (f.is_open())
            f << srt::core::JsonValue(std::move(wordsArr)).toJson(2);
    }

    return result;
}

} // namespace dsinfer_cli
