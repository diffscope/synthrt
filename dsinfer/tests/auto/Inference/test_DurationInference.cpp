#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include <synthrt/Core/SynthUnit.h>
#include <synthrt/SVS/InferenceContrib.h>

#include <dsinfer/Api/Drivers/Onnx/OnnxDriverApi.h>
#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>
#include <dsinfer/Api/Inferences/Duration/1/DurationApiL1.h>
#include <dsinfer/Inference/InferenceDriverFactory.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

namespace Co = ds::Api::Common::L1;
namespace Dur = ds::Api::Duration::L1;
namespace Onnx = ds::Api::Onnx;
namespace fs = std::filesystem;

// The duration predictor declares the word inputs of the word level linguistic encoder only when it
// consumes them. A predictor of the current DiffSinger exports takes them when it locates the
// phonemes with the word division, and when it splits the frame budget of every word it takes the
// budget as well. A duration model of an earlier export takes neither, and the ONNX session rejects
// both a missing and an unexpected input, so the configuration decides which inputs the task hands
// over. These cases run the complete duration task against models of every signature.
//
// The models are far smaller than real ones. The budget predictor adds the one based position of a
// phoneme inside its word to the frame budget of that word, so its shares depend on both word
// inputs and the test can check the values that reach it. The division predictor predicts the
// position alone, which checks the division without a budget. The absolute predictor predicts the
// MIDI pitch of the phoneme, which is constant inside a word, so it takes no word input and its
// shares are even. The task applies the shares of every word to the duration the score assigns to
// it.

namespace {

    // A payload travels by move and never by copy, and the factory below relies on that when it
    // returns the input it built. A payload type that loses either property breaks this file.
    static_assert(std::is_move_constructible_v<Dur::DurationStartInput>);
    static_assert(!std::is_copy_constructible_v<Dur::DurationStartInput>);

#ifdef TEST_RESOURCE_DIRECTORY
    const auto testResourceDirectory = fs::path(TEST_RESOURCE_DIRECTORY);
#endif

    constexpr const char *WORD_ENCODER_MODEL = "duration_word_encoder.onnx";
    constexpr const char *WORD_PREDICTOR_MODEL = "duration_word_predictor.onnx";
    constexpr const char *DIVISION_PREDICTOR_MODEL = "duration_division_predictor.onnx";
    constexpr const char *ABSOLUTE_PREDICTOR_MODEL = "duration_absolute_predictor.onnx";

    // The score of every case: two words of three and two phonemes, which last 0.5 and 0.4 seconds.
    const std::vector<std::vector<std::string>> WORD_TOKENS = {
        {"a", "i", "u"},
        {"e", "o"}
    };
    const std::vector<std::vector<double>> WORDS_PHONE_START = {
        {0.0, 0.2, 0.4},
        {0.0, 0.3}
    };
    const std::vector<int> WORD_NOTE_KEY = {60, 62};
    const std::vector<double> WORD_DURATION = {0.5, 0.4};

    class TemporaryDirectory {
    public:
        TemporaryDirectory() {
            static int sequence = 0;
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            m_path = fs::temp_directory_path() / ("dsinfer-duration-auto-" + std::to_string(stamp) +
                                                  "-" + std::to_string(sequence++));
            fs::create_directories(m_path);
        }

        ~TemporaryDirectory() {
            std::error_code error;
            fs::remove_all(m_path, error);
        }

        const fs::path &path() const {
            return m_path;
        }

    private:
        fs::path m_path;
    };

    void writeText(const fs::path &path, const std::string &text) {
        fs::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary);
        BOOST_REQUIRE(stream.is_open());
        stream << text;
        BOOST_REQUIRE(stream.good());
    }

#ifdef TEST_RESOURCE_DIRECTORY
    void copyModel(const fs::path &root, const char *name) {
        fs::copy_file(testResourceDirectory / name, root / name,
                      fs::copy_options::overwrite_existing);
    }
#endif

    // Describes the word inputs a configuration declares for the predictor. \c Invalid is not a
    // signature any model declares, it stands for a configuration that writes a value the schema
    // does not define, which must be rejected while the configuration is interpreted.
    enum class WordInputs {
        None,
        Division,
        DivisionAndBudget,
        Invalid,
    };

    // Writes one package holding a single duration contribution. The word inputs describe the
    // signature of the exported predictor, as the model itself declares it.
    void writePackage(const fs::path &root, const char *predictor, WordInputs wordInputs) {
        writeText(root / "desc.json",
                  R"({
                      "$version":"1.0",
                      "id":"duration-auto-test",
                      "version":"1",
                      "runtimeLevel":1,
                      "contributions":{
                        "inference":[
                          {"id":"duration","path":"duration.json"}
                        ]
                      }
                  })");
        writeText(root / "phonemes.json", R"({"SP":0,"a":1,"i":2,"u":3,"e":4,"o":5})");

        std::string configuration = R"({
                      "interface":"org.openvpi.dsinfer.inference.Duration",
                      "variant":"onnx",
                      "level":1,
                      "exports":{},
                      "configuration":{
                        "phonemes":"phonemes.json",
                        "encoder":")";
        configuration += WORD_ENCODER_MODEL;
        configuration += R"(",
                        "predictor":")";
        configuration += predictor;
        configuration += R"(",
                        "sampleRate":44100,
                        "hopSize":512)";
        switch (wordInputs) {
            case WordInputs::None:
                break;
            case WordInputs::Division:
                configuration += R"(,
                        "dur_type":"abs")";
                break;
            case WordInputs::DivisionAndBudget:
                configuration += R"(,
                        "dur_type":"rel")";
                break;
            case WordInputs::Invalid:
                // The option that spelled a budget without a division is gone. An unknown value
                // must be rejected while the configuration is interpreted.
                configuration += R"(,
                        "dur_type":"budget")";
                break;
        }
        configuration += R"(
                      }
                  })";
        writeText(root / "duration.json", configuration);
    }

#if defined(DSINFER_TEST_DRIVER_PLUGIN_PATH) && defined(DSINFER_TEST_INFERENCE_PLUGIN_PATH)
    // Loads the ONNX driver plugin into a SynthUnit. The driver retains the plugin code that the
    // factory loaded, so the factory is released after the unit that owns the driver.
    class ConfiguredSynthUnit final {
    public:
        ConfiguredSynthUnit() {
            const std::array<fs::path, 1> inferencePaths = {DSINFER_TEST_INFERENCE_PLUGIN_PATH};
            m_unit.setPluginPaths(srt::InferenceCategory::NAME, inferencePaths);

            const std::array<fs::path, 1> driverPaths = {DSINFER_TEST_DRIVER_PLUGIN_PATH};
            m_factory.setPluginPaths(driverPaths);
            auto loader = m_factory.find(Onnx::API_NAME);
            BOOST_REQUIRE(loader);
            auto driverResult = m_factory.create(loader);
            BOOST_REQUIRE(driverResult);
            auto driver = driverResult.take();

            Onnx::DriverInitArgs args;
            args.runtimePath = DSINFER_TEST_ORT_RUNTIME_DIR;
            BOOST_REQUIRE(driver->initialize(args));
            BOOST_REQUIRE(m_unit.addRuntimeService(std::move(driver)));
        }

        srt::SynthUnit &unit() {
            return m_unit;
        }

    private:
        ds::InferenceDriverFactory m_factory;
        srt::SynthUnit m_unit;
    };
#endif

#ifdef TEST_RESOURCE_DIRECTORY
    Dur::DurationStartInput makeStartInput() {
        Dur::DurationStartInput input;
        for (const auto wordDuration : WORD_DURATION) {
            input.duration += wordDuration;
        }
        for (size_t index = 0; index < WORD_TOKENS.size(); ++index) {
            Co::InputWordInfo word;
            for (size_t phone = 0; phone < WORD_TOKENS[index].size(); ++phone) {
                Co::InputPhonemeInfo phoneme;
                phoneme.token = WORD_TOKENS[index][phone];
                phoneme.start = WORDS_PHONE_START[index][phone];
                word.phones.push_back(phoneme);
            }
            Co::InputNoteInfo note;
            note.key = WORD_NOTE_KEY[index];
            note.duration = WORD_DURATION[index];
            word.notes.push_back(note);
            input.words.push_back(word);
        }
        return input;
    }

    /// Checks the predicted duration of every phoneme against its share of the word it belongs to,
    /// and checks that every word keeps the duration the score assigns to it.
    void checkShares(const std::vector<double> &durations, const std::vector<double> &shares) {
        BOOST_REQUIRE_EQUAL(durations.size(), shares.size());

        size_t index = 0;
        for (size_t word = 0; word < WORD_TOKENS.size(); ++word) {
            const auto phoneCount = WORD_TOKENS[word].size();
            double wordSum = 0;
            double shareSum = 0;
            for (size_t phone = 0; phone < phoneCount; ++phone) {
                BOOST_CHECK_CLOSE(durations[index], WORD_DURATION[word] * shares[index], 1e-4);
                wordSum += durations[index];
                shareSum += shares[index];
                ++index;
            }
            BOOST_CHECK_CLOSE(shareSum, 1.0, 1e-4);
            BOOST_CHECK_CLOSE(wordSum, WORD_DURATION[word], 1e-4);
        }
    }
#endif

#if defined(TEST_RESOURCE_DIRECTORY) && defined(DSINFER_TEST_DRIVER_PLUGIN_PATH) &&                \
    defined(DSINFER_TEST_INFERENCE_PLUGIN_PATH)
    // Holds the package of a case open in a SynthUnit and exposes the initialized task of its
    // duration contribution, so that a case writes only its configuration and its assertion.
    class DurationFixture {
    public:
        explicit DurationFixture(const fs::path &root) {
            auto opened = m_unit.unit().openPackage(root, srt::SynthUnit::Load);
            const std::string openWhy = opened ? std::string() : opened.error().toString();
            BOOST_REQUIRE_MESSAGE(bool(opened), openWhy);
            m_package = opened.take();

            auto spec = m_package.contribution("inference", "duration");
            BOOST_REQUIRE(spec);
            auto inference = spec->as<srt::InferenceSpec>();
            auto executiveResult = inference->createInference(Dur::DurationImportOptions(),
                                                              Dur::DurationRuntimeOptions());
            const std::string createWhy =
                executiveResult ? std::string() : executiveResult.error().toString();
            BOOST_REQUIRE_MESSAGE(bool(executiveResult), createWhy);
            m_executive = executiveResult.take();
            m_duration = m_executive->as<Dur::DurationExecutive>();

            auto initialized = m_duration->initialize(Dur::DurationInitArgs());
            const std::string initWhy =
                initialized ? std::string() : initialized.error().toString();
            BOOST_REQUIRE_MESSAGE(bool(initialized), initWhy);
        }

        Dur::DurationExecutive &duration() {
            return *m_duration;
        }

    private:
        ConfiguredSynthUnit m_unit;
        srt::PackageHandle m_package;
        std::unique_ptr<srt::InferenceExecutive> m_executive;
        Dur::DurationExecutive *m_duration = nullptr;
    };
#endif

}

BOOST_AUTO_TEST_SUITE(test_DurationInference)

#if defined(TEST_RESOURCE_DIRECTORY) && defined(DSINFER_TEST_DRIVER_PLUGIN_PATH) &&                \
    defined(DSINFER_TEST_INFERENCE_PLUGIN_PATH)
BOOST_AUTO_TEST_CASE(test_word_budget_predictor_receives_the_word_structure) {
    TemporaryDirectory temporary;
    writePackage(temporary.path(), WORD_PREDICTOR_MODEL, WordInputs::DivisionAndBudget);
    copyModel(temporary.path(), WORD_ENCODER_MODEL);
    copyModel(temporary.path(), WORD_PREDICTOR_MODEL);

    DurationFixture fixture(temporary.path());
    auto result = fixture.duration().start(makeStartInput());
    const std::string why = result ? std::string() : result.error().toString();
    BOOST_REQUIRE_MESSAGE(bool(result), why);
    const auto &durations = (*result)->durations;
    BOOST_REQUIRE_EQUAL(durations.size(), 5u);

    // The predictor adds the one based position of a phoneme to the frame budget of its word, so
    // the shares of a word depend on that budget. The word level linguistic encoder rounds the end
    // of a word once for the whole utterance, which gives the two words of the score 43 and 35
    // frames at the frame width of the configuration. The prediction of the word of three phonemes
    // is therefore 44, 45 and 46, and that of the word of two phonemes is 36 and 37.
    checkShares(durations, {44.0 / 135, 45.0 / 135, 46.0 / 135, 36.0 / 73, 37.0 / 73});
}

BOOST_AUTO_TEST_CASE(test_division_predictor_receives_the_word_division_only) {
    TemporaryDirectory temporary;
    writePackage(temporary.path(), DIVISION_PREDICTOR_MODEL, WordInputs::Division);
    copyModel(temporary.path(), WORD_ENCODER_MODEL);
    copyModel(temporary.path(), DIVISION_PREDICTOR_MODEL);

    DurationFixture fixture(temporary.path());
    auto result = fixture.duration().start(makeStartInput());
    const std::string why = result ? std::string() : result.error().toString();
    BOOST_REQUIRE_MESSAGE(bool(result), why);
    const auto &durations = (*result)->durations;
    BOOST_REQUIRE_EQUAL(durations.size(), 5u);

    // The predictor locates the word of a phoneme with the division and predicts the one based
    // position of the phoneme inside it, so the shares of a word of three phonemes are 1/6, 2/6 and
    // 3/6, and those of a word of two phonemes are 1/3 and 2/3. A division that does not match the
    // score locates the phonemes in other words and moves the shares away from this ramp.
    checkShares(durations, {1.0 / 6, 2.0 / 6, 3.0 / 6, 1.0 / 3, 2.0 / 3});
}

BOOST_AUTO_TEST_CASE(test_absolute_predictor_keeps_working_without_word_inputs) {
    TemporaryDirectory temporary;
    writePackage(temporary.path(), ABSOLUTE_PREDICTOR_MODEL, WordInputs::None);
    copyModel(temporary.path(), WORD_ENCODER_MODEL);
    copyModel(temporary.path(), ABSOLUTE_PREDICTOR_MODEL);

    DurationFixture fixture(temporary.path());
    auto result = fixture.duration().start(makeStartInput());
    const std::string why = result ? std::string() : result.error().toString();
    BOOST_REQUIRE_MESSAGE(bool(result), why);
    const auto &durations = (*result)->durations;
    BOOST_REQUIRE_EQUAL(durations.size(), 5u);

    // The predictor takes no word input and predicts the same value for every phoneme of a word, so
    // every word keeps its duration and is divided evenly.
    checkShares(durations, {1.0 / 3, 1.0 / 3, 1.0 / 3, 1.0 / 2, 1.0 / 2});
}

BOOST_AUTO_TEST_CASE(test_word_inputs_of_an_unconfigured_predictor_are_reported) {
    TemporaryDirectory temporary;
    writePackage(temporary.path(), WORD_PREDICTOR_MODEL, WordInputs::None);
    copyModel(temporary.path(), WORD_ENCODER_MODEL);
    copyModel(temporary.path(), WORD_PREDICTOR_MODEL);

    DurationFixture fixture(temporary.path());
    auto result = fixture.duration().start(makeStartInput());
    BOOST_REQUIRE(!result);
    const auto message = result.error().toString();
    BOOST_CHECK_MESSAGE(message.find("word_div") != std::string::npos, message);
}

BOOST_AUTO_TEST_CASE(test_unexpected_word_inputs_are_reported) {
    TemporaryDirectory temporary;
    writePackage(temporary.path(), ABSOLUTE_PREDICTOR_MODEL, WordInputs::DivisionAndBudget);
    copyModel(temporary.path(), WORD_ENCODER_MODEL);
    copyModel(temporary.path(), ABSOLUTE_PREDICTOR_MODEL);

    DurationFixture fixture(temporary.path());
    auto result = fixture.duration().start(makeStartInput());
    BOOST_REQUIRE(!result);
    const auto message = result.error().toString();
    BOOST_CHECK_MESSAGE(message.find("word_div") != std::string::npos, message);
}
#endif

#if defined(TEST_RESOURCE_DIRECTORY) && defined(DSINFER_TEST_INFERENCE_PLUGIN_PATH)
BOOST_AUTO_TEST_CASE(test_an_unknown_duration_type_is_rejected) {
    TemporaryDirectory temporary;
    writePackage(temporary.path(), WORD_PREDICTOR_MODEL, WordInputs::Invalid);
    copyModel(temporary.path(), WORD_ENCODER_MODEL);
    copyModel(temporary.path(), WORD_PREDICTOR_MODEL);

    // A configuration is interpreted while the Package is loaded, so this case needs no driver.
    srt::SynthUnit unit;
    const std::array<fs::path, 1> inferencePaths = {DSINFER_TEST_INFERENCE_PLUGIN_PATH};
    unit.setPluginPaths(srt::InferenceCategory::NAME, inferencePaths);

    auto opened = unit.openPackage(temporary.path(), srt::SynthUnit::Load);
    BOOST_REQUIRE(!opened);
    const auto message = opened.error().toString();
    BOOST_CHECK_MESSAGE(message.find("dur_type") != std::string::npos, message);
}
#endif

BOOST_AUTO_TEST_SUITE_END()
