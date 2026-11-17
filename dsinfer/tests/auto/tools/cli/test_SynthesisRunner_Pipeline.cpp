#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <synthrt/SVS/InferenceContrib.h>
#include <synthrt/SVS/SingerContrib.h>

#include "SynthesisRunner.h"

#include "../../plugins/inferenceinterpreters/RecordingDriver.h"

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

namespace fs = std::filesystem;

namespace {
    struct PipelineHost {
        fs::path collection =
            fs::temp_directory_path() /
            ("synthesis-pipeline-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::path root = collection / "voice";
        std::shared_ptr<ds::test::DriverTrace> trace = std::make_shared<ds::test::DriverTrace>();
        srt::SynthUnit unit;

        PipelineHost() {
            fs::create_directories(root);
            BOOST_REQUIRE(
                unit.addRuntimeService(std::make_unique<ds::test::RecordingDriver>(trace)));
            unit.setPluginPaths(srt::InferenceCategory::NAME, {DSINFER_TEST_INFERENCE_PLUGINS});
            unit.setPluginPaths(srt::SingerCategory::NAME, {DSINFER_TEST_SINGER_PLUGINS});
            srt::JsonArray modules;
            srt::JsonArray imports;
            for (const auto name : {"Duration", "Pitch", "Variance", "Acoustic", "Vocoder"}) {
                const std::string kind(name);
                modules.emplace_back(srt::JsonObject{
                    {"id",   kind          },
                    {"path", kind + ".json"}
                });
                srt::JsonObject configuration{
                    {"phonemes",                  "phonemes.json"       },
                    {"model",                     kind + ".onnx"        },
                    {"encoder",                   kind + "-encoder.onnx"},
                    {"predictor",                 kind + ".onnx"        },
                    {"sampleRate",                100                   },
                    {"hopSize",                   1                     },
                    {"frameWidth",                0.01                  },
                    {"melChannels",               2                     },
                    {"maxDepth",                  1000                  },
                    {"useContinuousAcceleration", false                 }
                };
                srt::JsonObject exports;
                srt::JsonObject options;
                if (kind == "Variance") {
                    exports.emplace("predictions", srt::JsonArray{"energy"});
                    options.emplace("predictions", srt::JsonArray{"energy"});
                }
                if (kind == "Acoustic") {
                    configuration.emplace("parameters", srt::JsonArray{"energy"});
                    exports.emplace("varianceControls", srt::JsonArray{"energy"});
                }
                write(kind + ".json", srt::JsonObject{
                                          {"interface",     "org.openvpi.dsinfer.inference." + kind},
                                          {"level",         1                                      },
                                          {"variant",       "onnx"                                 },
                                          {"exports",       exports                                },
                                          {"configuration", configuration                          }
                });
                std::string role(kind);
                for (auto &character : role) {
                    if (character >= 'A' && character <= 'Z') {
                        character = static_cast<char>(character - 'A' + 'a');
                    }
                }
                imports.emplace_back(srt::JsonObject{
                    {"role",    "singer/" + role    },
                    {"ref",     ":inference/" + kind},
                    {"options", options             }
                });
            }
            write("phonemes.json", srt::JsonObject{
                                       {"a", 1}
            });
            write("singer.json", srt::JsonObject{
                                     {"interface",     "org.openvpi.dsinfer.singer.DiffSinger"    },
                                     {"level",         1                                          },
                                     {"variant",       "openvpi"                                  },
                                     {"exports",       srt::JsonObject()                          },
                                     {"configuration", srt::JsonObject{{"dict", "dictionary.txt"}}},
                                     {"imports",       imports                                    }
            });
            write(
                "desc.json",
                srt::JsonObject{
                    {"$version",      "1.0"                                                     },
                    {"id",            "pipeline-test"                                           },
                    {"version",       "1"                                                       },
                    {"runtimeLevel",  1                                                         },
                    {"contributions",
                     srt::JsonObject{{"inference", modules},
                                     {"singer", srt::JsonArray{srt::JsonObject{
                                                    {"id", "voice"}, {"path", "singer.json"}}}}}}
            });
        }
        ~PipelineHost() {
            std::error_code error;
            fs::remove_all(collection, error);
        }

        void write(const std::string &name, srt::JsonValue value) {
            std::ofstream stream(root / name, std::ios::binary);
            stream << value.toJson();
            BOOST_REQUIRE(stream.good());
        }

        ds::cli::SynthesisInput input(bool supplyCurves = false) {
            ds::cli::SynthesisInput input;
            input.singer = "voice";
            input.acoustic = std::make_unique<ds::Api::Acoustic::L1::AcousticStartInput>();
            input.acoustic->duration = 0.04;
            input.acoustic->steps = 20;
            input.acoustic->depth = 0.75f;
            ds::Api::Common::L1::InputWordInfo word;
            word.phones.push_back({"a", "", 0, 0});
            word.notes.push_back({69, 0, 0.04});
            input.acoustic->words.push_back(word);
            if (supplyCurves) {
                input.acoustic->parameters.push_back(
                    {ds::Api::Common::L1::Tags::Pitch, {69}, 0.01});
                input.acoustic->parameters.push_back(
                    {ds::Api::Common::L1::Tags::Energy, {1}, 0.01});
            }
            return input;
        }
    };

    std::vector<char> readWaveform(const fs::path &path) {
        std::ifstream stream(path, std::ios::binary);
        std::vector<char> file((std::istreambuf_iterator<char>(stream)), {});
        BOOST_REQUIRE_GE(file.size(), 44u);
        BOOST_CHECK_EQUAL(std::string(file.data(), 4), "RIFF");
        BOOST_CHECK_EQUAL(std::string(file.data() + 8, 4), "WAVE");
        size_t offset = 12;
        while (offset + 8 <= file.size()) {
            uint32_t size = 0;
            std::memcpy(&size, file.data() + offset + 4, sizeof(size));
            BOOST_REQUIRE_LE(offset + 8 + size, file.size());
            if (std::string(file.data() + offset, 4) == "data") {
                return {file.begin() + offset + 8, file.begin() + offset + 8 + size};
            }
            offset += 8 + size + (size % 2);
        }
        BOOST_FAIL("the output contains no waveform data chunk");
        return {};
    }
}

BOOST_AUTO_TEST_CASE(completePipelineUsesEveryStageAndReleasesEverySession) {
    for (const bool supplyCurves : {false, true}) {
        PipelineHost host;
        ds::cli::SynthesisRunner runner(host.unit);
        const auto output = host.root / "output.wav";
        runner.run(host.root, host.input(supplyCurves), output);
        const auto bytes = readWaveform(output);
        const std::vector<float> expected{2, 2, 2, 2};
        BOOST_REQUIRE_EQUAL(bytes.size(), expected.size() * sizeof(float));
        BOOST_CHECK_EQUAL(std::memcmp(bytes.data(), expected.data(), bytes.size()), 0);
        BOOST_CHECK_EQUAL(host.trace->created.load(), 8);
        BOOST_CHECK_EQUAL(host.trace->destroyed.load(), 8);
        BOOST_REQUIRE_EQUAL(host.trace->calls.size(), 8u);
        BOOST_CHECK_EQUAL(host.trace->calls[0].model.filename().string(), "Duration-encoder.onnx");
        BOOST_CHECK_EQUAL(host.trace->calls[7].model.filename().string(), "Vocoder.onnx");
        const auto &acoustic = host.trace->calls[6];
        BOOST_CHECK_EQUAL(acoustic.inputs.at("energy")->data<float>()[0], 2);
        BOOST_CHECK_CLOSE(acoustic.inputs.at("f0")->data<float>()[0],
                          440.0 * std::exp2((2.0 - 69.0) / 12.0), 0.0001);
        BOOST_CHECK(host.unit.loadedPackages().empty());
    }
}

BOOST_AUTO_TEST_CASE(stageFailuresProduceNoOutputAndReleaseThePipeline) {
    for (const auto model :
         {"Duration.onnx", "Pitch.onnx", "Variance.onnx", "Acoustic.onnx", "Vocoder.onnx"}) {
        PipelineHost host;
        host.trace->fault = ds::test::SessionFault::Run;
        host.trace->faultModel = model;
        ds::cli::SynthesisRunner runner(host.unit);
        const auto output = host.root / "output.wav";
        BOOST_CHECK_THROW(runner.run(host.root, host.input(), output), std::runtime_error);
        BOOST_CHECK(!fs::exists(output));
        BOOST_CHECK_EQUAL(host.trace->created.load(), host.trace->destroyed.load());
        BOOST_CHECK(host.unit.loadedPackages().empty());
    }
}

BOOST_AUTO_TEST_CASE(unknownSingerIsRejectedBeforeAnyModelIsOpened) {
    PipelineHost host;
    ds::cli::SynthesisRunner runner(host.unit);
    auto input = host.input();
    input.singer = "missing";
    BOOST_CHECK_THROW(runner.run(host.root, std::move(input), host.root / "output.wav"),
                      std::runtime_error);
    BOOST_CHECK_EQUAL(host.trace->created.load(), 0);
    BOOST_CHECK(host.unit.loadedPackages().empty());
}
