#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <stdcorelib/console.h>
#include <stdcorelib/path.h>
#include <stdcorelib/str.h>
#include <stdcorelib/system.h>

#include <synthrt/Core/Support/JSON.h>
#include <synthrt/Core/Support/Logging.h>
#include <synthrt/Core/Core/Runtime.h>
#include <synthrt/Core/Task/ITask.h>
#include <synthrt/Core/Tensor/ITensor.h>

#include <diffsinger/Bank/VoicebankScanner.h>
#include <synthrt/G2P/LanguageService.h>
#include <diffsinger/Infer/InferenceRequest.h>
#include <diffsinger/Infer/InferenceResult.h>
#include <diffsinger/Infer/InferenceService.h>
#include <diffsinger/Infer/ModelSet.h>
#include <diffsinger/Infer/StageKind.h>
#include <diffsinger/Infer/SingerStageResolver.h>
#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>
#include <dsinfer/Api/Inferences/Acoustic/1/AcousticApiL1.h>
#include <dsinfer/Api/Inferences/Duration/1/DurationApiL1.h>
#include <dsinfer/Api/Inferences/Pitch/1/PitchApiL1.h>
#include <dsinfer/Api/Inferences/Variance/1/VarianceApiL1.h>
#include <dsinfer/Api/Inferences/Vocoder/1/VocoderApiL1.h>
#include <synthrt/Driver/OnnxSetup.h>

#include "CliArgs.h"
#include "cli_log.h"
#include "midi_parser.h"
#include "dspx_parser.h"
#include "segment_builder.h"
#include "g2p_s2p_pipeline.h"
#include "note_data.h"
#include "wav_writer.h"

namespace fs = std::filesystem;

namespace dsinfer_cli {

    // ============================================================================
    // Lite-style pipeline (Phase 6): uses ModelSet for per-stage lazy load +
    // lifecycle management, mirroring the planned lite integration path.
    // Produces the same InferenceResult as InferenceService::run but exercises
    // ModelSet::load/stop/unload/unloadAll.
    // ============================================================================
    static ds::infer::InferenceResult runLiteStylePipeline(const ds::infer::StageSet &stages,
                                                 const ds::infer::InferenceRequest &request) {
        namespace Co = srt::svs::Api::Common::L1;
        namespace Ac = srt::svs::Api::Acoustic::L1;
        namespace Dur = srt::svs::Api::Duration::L1;
        namespace Pit = srt::svs::Api::Pitch::L1;
        namespace Var = srt::svs::Api::Variance::L1;
        namespace Vo = srt::svs::Api::Vocoder::L1;
        using srt::core::NO;

        ds::infer::InferenceResult result;

        // 0. Validate all stage specs are non-null (mirrors InferenceService::setStages).
        //    Resolver may return null specs if the singer package is incomplete;
        //    without this check, stages.variance.spec->schema() etc. would crash.
        if (!stages.duration.spec || !stages.pitch.spec || !stages.variance.spec ||
            !stages.acoustic.spec || !stages.vocoder.spec) {
            result.error.code = srt::core::ErrorCode::InvalidArgument;
            result.error.severity = srt::core::Severity::Error;
            result.error.message = "runLiteStylePipeline: one or more stage specs are null";
            return result;
        }

        // 1. Create ModelSet from stages
        ds::infer::ModelSet modelSet(stages);

        // Helper: start an inference and extract typed result.
        // Checks inference->state() == Failed after start (mirrors
        // InferenceService::startAndCheck) to catch cases where start()
        // returns success but the inference internally failed.
        auto startStage = [&](ds::infer::StageKind kind, const NO<srt::core::TaskStartInput> &input,
                               const std::string &stageName) -> srt::core::Expected<NO<srt::core::TaskResult>> {
            auto loadExp = modelSet.load(kind);
            if (!loadExp) {
                return srt::core::Error(srt::core::Error::SessionError,
                                        "ModelSet: failed to load " + stageName + ": " + loadExp.error().message());
            }
            auto *inference = *loadExp;
            auto startExp = inference->start(input);
            if (!startExp) {
                return srt::core::Error(srt::core::Error::SessionError,
                                        "ModelSet: failed to start " + stageName + ": " + startExp.error().message());
            }
            auto taskResult = startExp.take();
            if (inference->state() == srt::core::ITask::Failed) {
                return srt::core::Error(srt::core::Error::SessionError,
                                        "ModelSet: " + stageName + " inference failed: " + taskResult->error.message());
            }
            return taskResult;
        };

        auto words = request.words;
        auto parameters = request.parameters;
        auto speakers = request.speakers;

        // --- Stage 1: Duration ---
        {
            auto input = NO<Dur::DurationStartInput>::create();
            input->words = words;
            auto resExp = startStage(ds::infer::StageKind::Duration, input, "duration");
            if (!resExp) {
                result.error = resExp.error().diagnostic();
                return result;
            }
            auto durResult = resExp.value().as<Dur::DurationResult>();
            if (!durResult || durResult->error.code() != srt::core::ErrorCode::None) {
                result.error = durResult ? durResult->error.diagnostic()
                                         : srt::core::Error(srt::core::Error::SessionError,
                                                              "duration result type mismatch").diagnostic();
                return result;
            }
            const auto &durations = durResult->durations;
            size_t i = 0;
            for (auto &word : words) {
                double timeCursor = 0.0;
                for (auto &phoneme : word.phones) {
                    if (i >= durations.size())
                        break;
                    phoneme.start = timeCursor;
                    timeCursor += durations[i];
                    ++i;
                }
            }
        }

        // --- Stage 2: Pitch ---
        {
            auto input = NO<Pit::PitchStartInput>::create();
            input->words = words;
            for (const auto &param : parameters) {
                if (param.tag == Co::Tags::Pitch || param.tag == Co::Tags::Expr) {
                    input->parameters.push_back(param);
                }
            }
            input->speakers = speakers;
            input->steps = request.steps;
            auto resExp = startStage(ds::infer::StageKind::Pitch, input, "pitch");
            if (!resExp) {
                result.error = resExp.error().diagnostic();
                return result;
            }
            auto pitResult = resExp.value().as<Pit::PitchResult>();
            if (!pitResult || pitResult->error.code() != srt::core::ErrorCode::None) {
                result.error = pitResult ? pitResult->error.diagnostic()
                                         : srt::core::Error(srt::core::Error::SessionError,
                                                              "pitch result type mismatch").diagnostic();
                return result;
            }
            bool hasPitch = false;
            for (auto &param : parameters) {
                if (param.tag == Co::Tags::Pitch) {
                    param.interval = pitResult->interval;
                    param.values = pitResult->pitch;
                    hasPitch = true;
                }
            }
            if (!hasPitch) {
                parameters.emplace_back(Co::InputParameterInfo{Co::Tags::Pitch, pitResult->pitch, pitResult->interval});
            }
        }

        // --- Stage 3: Variance ---
        {
            const auto schema = stages.variance.spec->schema().as<Var::VarianceSchema>();
            auto input = NO<Var::VarianceStartInput>::create();
            input->words = words;
            for (const auto &param : parameters) {
                if (param.tag == Co::Tags::Pitch) {
                    input->parameters.push_back(param);
                    continue;
                }
                if (schema) {
                    for (const auto &prediction : schema->predictions) {
                        if (prediction == param.tag) {
                            input->parameters.push_back(param);
                        }
                    }
                }
            }
            input->speakers = speakers;
            input->steps = request.steps;
            auto resExp = startStage(ds::infer::StageKind::Variance, input, "variance");
            if (!resExp) {
                result.error = resExp.error().diagnostic();
                return result;
            }
            auto varResult = resExp.value().as<Var::VarianceResult>();
            if (!varResult || varResult->error.code() != srt::core::ErrorCode::None) {
                result.error = varResult ? varResult->error.diagnostic()
                                         : srt::core::Error(srt::core::Error::SessionError,
                                                              "variance result type mismatch").diagnostic();
                return result;
            }
            auto &predictions = varResult->predictions;
            if (schema) {
                const auto nPreds = schema->predictions.size();
                std::vector<char> satisfied(nPreds, false);
                for (size_t i = 0; i < nPreds; ++i) {
                    auto it = std::find_if(parameters.begin(), parameters.end(),
                                           [&](const auto &p) { return p.tag == schema->predictions[i]; });
                    if (it == parameters.end())
                        continue;
                    for (auto &predicted : predictions) {
                        if (it->tag == predicted.tag) {
                            it->interval = predicted.interval;
                            it->values = std::move(predicted.values);
                            it->retake = std::nullopt;
                            satisfied[i] = true;
                            break;
                        }
                    }
                }
                for (size_t i = 0; i < nPreds; ++i) {
                    if (satisfied[i])
                        continue;
                    for (auto &predicted : predictions) {
                        if (predicted.tag == schema->predictions[i]) {
                            parameters.emplace_back(std::move(predicted));
                            break;
                        }
                    }
                }
            }
        }

        // --- Stage 4: Acoustic ---
        NO<srt::core::ITensor> mel;
        NO<srt::core::ITensor> f0;
        {
            const auto acConfig = stages.acoustic.spec->configuration().as<Ac::AcousticConfiguration>();
            if (acConfig) {
                const auto &cfgParams = acConfig->parameters;
                auto hasParam = [&](const srt::svs::ParamTag &tag) {
                    return std::any_of(parameters.begin(), parameters.end(),
                                       [&](const auto &p) { return p.tag == tag; });
                };
                if (cfgParams.find(Co::Tags::Gender) != cfgParams.end() &&
                    !hasParam(Co::Tags::Gender)) {
                    parameters.push_back({Co::Tags::Gender, {0.0}, request.duration});
                }
                if (cfgParams.find(Co::Tags::Velocity) != cfgParams.end() &&
                    !hasParam(Co::Tags::Velocity)) {
                    parameters.push_back({Co::Tags::Velocity, {1.0}, request.duration});
                }
            }

            auto input = NO<Ac::AcousticStartInput>::create();
            input->words = words;
            input->parameters = parameters;
            input->speakers = speakers;
            input->duration = request.duration;
            input->steps = request.steps;
            input->depth = request.depth;
            auto resExp = startStage(ds::infer::StageKind::Acoustic, input, "acoustic");
            if (!resExp) {
                result.error = resExp.error().diagnostic();
                return result;
            }
            auto acResult = resExp.value().as<Ac::AcousticResult>();
            if (!acResult || acResult->error.code() != srt::core::ErrorCode::None) {
                result.error = acResult ? acResult->error.diagnostic()
                                         : srt::core::Error(srt::core::Error::SessionError,
                                                              "acoustic result type mismatch").diagnostic();
                return result;
            }
            mel = acResult->mel;
            f0 = acResult->f0;
        }

        // --- Stage 5: Vocoder ---
        {
            auto input = NO<Vo::VocoderStartInput>::create();
            input->mel = mel;
            input->f0 = f0;
            auto resExp = startStage(ds::infer::StageKind::Vocoder, input, "vocoder");
            if (!resExp) {
                result.error = resExp.error().diagnostic();
                return result;
            }
            auto voResult = resExp.value().as<Vo::VocoderResult>();
            if (!voResult || voResult->error.code() != srt::core::ErrorCode::None) {
                result.error = voResult ? voResult->error.diagnostic()
                                         : srt::core::Error(srt::core::Error::SessionError,
                                                              "vocoder result type mismatch").diagnostic();
                return result;
            }
            const auto &audioData = voResult->audioData;
            if (!audioData.empty()) {
                const auto sampleCount = audioData.size() / sizeof(float);
                result.audio.resize(sampleCount);
                std::memcpy(result.audio.data(), audioData.data(), audioData.size());
            }
            const auto vocoderConfig = stages.vocoder.spec->configuration().as<Vo::VocoderConfiguration>();
            if (vocoderConfig) {
                result.sampleRate = vocoderConfig->sampleRate;
            }
            result.channels = 1;
        }

        // --- Lifecycle test: stop, unload, reload, unloadAll ---
        // Exercises ModelSet::stop (per Phase 6 doc: start→stop sequence),
        // then unload/reload/unloadAll to verify full lifecycle.
        {
            // stop(Duration): test stop on a completed (non-Running) inference.
            // start() is synchronous, so the task is already done; stop may
            // succeed or return false depending on the driver implementation.
            // We log but don't treat stop failure as fatal here.
            cliLog.srtInfo("Lite-style lifecycle test: stop(Duration)");
            auto stopExp = modelSet.stop(ds::infer::StageKind::Duration);
            if (!stopExp) {
                cliLog.srtInfo("  stop(Duration) returned: " + stopExp.error().message() +
                               " (expected if task already completed)");
            } else {
                cliLog.srtInfo("  ok: stop(Duration) succeeded");
            }

            cliLog.srtInfo("Lite-style lifecycle test: unload(Vocoder)");
            auto unloadExp = modelSet.unload(ds::infer::StageKind::Vocoder);
            if (!unloadExp) {
                cliLog.srtWarning("unload(Vocoder) failed: " + unloadExp.error().message());
            } else if (modelSet.isLoaded(ds::infer::StageKind::Vocoder)) {
                cliLog.srtWarning("isLoaded(Vocoder) should be false after unload");
            } else {
                cliLog.srtInfo("  ok: Vocoder unloaded, isLoaded=false");
            }

            cliLog.srtInfo("Lite-style lifecycle test: reload(Vocoder)");
            auto reloadExp = modelSet.load(ds::infer::StageKind::Vocoder);
            if (!reloadExp) {
                cliLog.srtWarning("reload(Vocoder) failed: " + reloadExp.error().message());
            } else if (!modelSet.isLoaded(ds::infer::StageKind::Vocoder)) {
                cliLog.srtWarning("isLoaded(Vocoder) should be true after reload");
            } else {
                cliLog.srtInfo("  ok: Vocoder reloaded, isLoaded=true");
            }

            cliLog.srtInfo("Lite-style lifecycle test: unloadAll()");
            auto unloadAllExp = modelSet.unloadAll();
            if (!unloadAllExp) {
                cliLog.srtWarning("unloadAll failed: " + unloadAllExp.error().message());
            } else {
                bool anyLoaded = false;
                for (auto kind : {ds::infer::StageKind::Duration, ds::infer::StageKind::Pitch,
                                   ds::infer::StageKind::Variance, ds::infer::StageKind::Acoustic,
                                   ds::infer::StageKind::Vocoder}) {
                    if (modelSet.isLoaded(kind)) {
                        anyLoaded = true;
                        break;
                    }
                }
                if (anyLoaded) {
                    cliLog.srtWarning("unloadAll: some stages still loaded");
                } else {
                    cliLog.srtInfo("  ok: all stages unloaded");
                }
            }
        }

        result.error.code = srt::core::ErrorCode::None;
        result.error.severity = srt::core::Severity::Info;
        result.error.message = "Lite-style pipeline completed";
        return result;
    }

    static int execPipeline(const CliArgs &args) {
        // 1. VoicebankScanner — scan desc.json for voicebank packages.
        //    G2P packages are handled separately by LanguageService.
        ds::bank::VoicebankScanner scanner;
        scanner.setSearchPaths({args.packageDir});

        auto statusesExp = scanner.refresh();
        if (!statusesExp) {
            cliLog.srtCritical("refresh failed: " + statusesExp.error().message());
            return -1;
        }
        for (const auto &s : *statusesExp) {
            if (s.valid) {
                cliLog.srtInfo(stdc::formatN("  package ok: id=%1, version=%2",
                                             s.packageId, s.version.toString()));
            } else {
                cliLog.srtWarning(stdc::formatN("  package invalid: %1 (%2)",
                                                stdc::path::to_utf8(s.rootPath), s.error.message));
            }
        }
        for (const auto &snap : scanner.singers()) {
            cliLog.srtInfo(stdc::formatN("  singer: packageId=%1, singerId=%2, name=%3",
                                         snap.ref.packageId, snap.ref.singerId, snap.name));
        }

        // 2. Build the packageDirs map (packageId -> directory) that
        //    LanguageService needs to resolve per-singer G2P routes.
        std::unordered_map<std::string, std::filesystem::path> packageDirs;
        for (const auto &s : scanner.singers()) {
            packageDirs[s.ref.packageId] = scanner.packageDirectory(s.ref.packageId);
        }

        // 3. LanguageService — G2P initialization (Stages 1-4) + convertLyric.
        //    Idempotent: skips when the G2P Manager singleton is initialized.
        ds::lang::LanguageService langSvc;
        auto langInitExp = langSvc.initialize(
            args.pluginPaths, args.g2pPackagePaths, packageDirs);
        if (!langInitExp) {
            cliLog.srtCritical("LanguageService initialize failed: " + langInitExp.error().message());
            return -1;
        }

        // 4. Runtime + ONNX driver (Stage 5). Plugin root is auto-derived
        //    from the application directory layout (<app>/../lib/plugins).
        srt::core::Runtime runtime;
        const auto appDir = stdc::system::application_directory();
        const auto pluginRoot = appDir.parent_path() /
                                stdc::path::from_utf8("lib") /
                                stdc::path::from_utf8("plugins");
        srt::driver::OnnxDriverConfig driverCfg;
        driverCfg.ep = args.ep;
        driverCfg.deviceIndex = args.deviceIndex;
        auto driverExp = srt::driver::setupOnnxInferenceDriver(runtime, pluginRoot, driverCfg);
        if (!driverExp) {
            cliLog.srtCritical("ONNX driver setup failed: " + driverExp.error().message());
            return -1;
        }

        // 5. Parse input (MIDI or DSPX -> notes)
        std::vector<MidiNote> notes;
        if (args.mode == "midi") {
            notes = parseMidi(args.inputPath);
        } else if (args.mode == "dspx") {
#ifdef DSINFER_CLI_HAS_OPENDSPX
            notes = parseDspx(args.inputPath);
#else
            cliLog.srtCritical(
                "DSPX parsing is not enabled (DSINFER_CLI_HAS_OPENDSPX not defined)");
            return -1;
#endif
        } else {
            printUsage();
            return 1;
        }

        // 6. Build continuous segments (~40s each for MIDI; single piece for DSPX)
        static constexpr double MAX_SEGMENT_SEC = 40.0;
        std::vector<MidiPiece> segments;
        if (args.mode == "dspx") {
            segments.push_back(buildContinuousPiece(notes));
        } else {
            segments = buildContinuousSegments(notes, MAX_SEGMENT_SEC);
        }
        if (segments.empty() || segments[0].notes.empty()) {
            cliLog.srtCritical("no segments found");
            return -1;
        }

        // Dump: 01_midi_notes.json + 02_segments.json
        if (!args.dumpDataDir.empty()) {
            std::error_code ec;
            fs::create_directories(args.dumpDataDir, ec);
            auto writeFile = [&](const char *name, const srt::core::JsonValue &val) {
                std::ofstream f(args.dumpDataDir / name, std::ios::binary);
                if (f.is_open())
                    f << val.toJson(2);
            };
            srt::core::JsonArray notesArr;
            for (const auto &n : notes) {
                srt::core::JsonObject nObj;
                nObj["startTick"] = static_cast<uint32_t>(n.startTick);
                nObj["endTick"] = static_cast<uint32_t>(n.endTick);
                nObj["key"] = n.key;
                nObj["lyric"] = n.lyric;
                nObj["language"] = n.language;
                nObj["startMs"] = n.startMs;
                nObj["endMs"] = n.endMs;
                notesArr.emplace_back(std::move(nObj));
            }
            writeFile("01_midi_notes.json", notesArr);

            srt::core::JsonArray segArr;
            for (const auto &seg : segments) {
                srt::core::JsonObject segObj;
                segObj["noteCount"] = static_cast<int>(seg.notes.size());
                if (!seg.notes.empty()) {
                    segObj["startMs"] = seg.notes.front().startMs;
                    segObj["endMs"] = seg.notes.back().endMs;
                }
                segArr.emplace_back(std::move(segObj));
            }
            writeFile("02_segments.json", segArr);
        }

        // 7. Find singer (derive singerId from packageDir filename)
        const auto singerId = stdc::path::to_utf8(args.packageDir.filename());
        auto refExp = scanner.findSinger(singerId);
        if (!refExp) {
            cliLog.srtCritical("findSinger failed: " + refExp.error().message());
            return -1;
        }
        const auto &ref = *refExp;

        // 8. G2P + S2P + buildWords for the first segment
        const auto &piece = segments[0];
        fs::create_directories(args.outputDir);
        auto outputPath = args.outputDir / "output.wav";
        const double totalSec =
            (piece.notes.back().endMs - piece.notes.front().startMs) / 1000.0;
        cliLog.srtInfo(stdc::formatN("Running piece (%1 sec, %2 notes) -> %3",
                                     totalSec, piece.notes.size(),
                                     stdc::path::to_utf8(outputPath)));

        auto input = buildInputFromPiece(langSvc, ref, piece, args.speakerId,
                                         args.languageId, args.dumpDataDir);

        // 9. Lazy-load the voicebank package into the Runtime (parses desc.json
        //    and registers singer/inference specs into Runtime categories).
        const auto pkgDir = scanner.packageDirectory(ref.packageId);
        if (pkgDir.empty()) {
            cliLog.srtCritical("package directory not found for packageId: " + ref.packageId);
            return -1;
        }
        auto loadExp = runtime.loadPackage(pkgDir);
        if (!loadExp) {
            cliLog.srtCritical("failed to load voicebank package: " + loadExp.error().message());
            return -1;
        }

        // 10. Resolve the 5 pipeline StageSpecs from the loaded singer imports.
        ds::infer::SingerStageResolver resolver;
        auto stagesExp = resolver.resolve(runtime, ref.packageId, ref.singerId, ref.version);
        if (!stagesExp) {
            cliLog.srtCritical("resolve stages failed: " + stagesExp.error().message());
            return -1;
        }
        const auto &stages = *stagesExp;

        // 11. Run the 5-stage pipeline. In lite-style mode, use ModelSet for
        // per-stage lazy load + lifecycle test; otherwise use InferenceService.
        ds::infer::InferenceRequest req;
        req.singerId = ref.singerId;
        req.inferenceId = "acoustic";
        req.speakerId = args.speakerId;
        req.words = input.input->words;
        req.parameters = input.input->parameters;
        req.speakers = input.input->speakers;
        req.duration = input.input->duration;
        req.steps = input.input->steps;

        // Dump: 08_inference_request.json
        if (!args.dumpDataDir.empty()) {
            srt::core::JsonObject reqObj;
            reqObj["singerId"] = req.singerId;
            reqObj["inferenceId"] = req.inferenceId;
            reqObj["speakerId"] = req.speakerId;
            reqObj["duration"] = req.duration;
            reqObj["steps"] = static_cast<int>(req.steps);
            reqObj["wordCount"] = static_cast<int>(req.words.size());
            std::ofstream f(args.dumpDataDir / "08_inference_request.json", std::ios::binary);
            if (f.is_open())
                f << srt::core::JsonValue(reqObj).toJson(2);
        }

        ds::infer::InferenceResult result;
        if (args.testLiteStyle) {
            cliLog.srtInfo("Running lite-style pipeline (ModelSet)");
            result = runLiteStylePipeline(stages, req);
        } else {
            ds::infer::InferenceService inferenceService;
            auto setStagesExp = inferenceService.setStages(
                stages.duration, stages.pitch, stages.variance,
                stages.acoustic, stages.vocoder);
            if (!setStagesExp) {
                cliLog.srtCritical("setStages failed: " + setStagesExp.error().message());
                return -1;
            }
            result = inferenceService.run(req);
        }
        if (result.error.code != srt::core::ErrorCode::None) {
            cliLog.srtCritical("runInference failed: " + result.error.message);
            return -1;
        }
        if (!result.sampleRate || result.audio.empty()) {
            cliLog.srtCritical("runInference failed: no audio output produced");
            return -1;
        }

        // 12. Write wav
        return writeWav(outputPath, result);
    }

} // namespace dsinfer_cli

int main(int argc, char *argv[]) {
    using namespace dsinfer_cli;

    CliArgs args;
    if (!args.parse(argc, argv)) {
        return args.exitCode;
    }

    installLogCallback();

    try {
        return execPipeline(args);
    } catch (const std::exception &e) {
        stdc::console::critical("Error: %1", e.what());
        return -1;
    }
}
