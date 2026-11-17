#include <system_error>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <dsinfer/Support/PhonemeDict.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

namespace {
    struct DictionaryFile {
        std::filesystem::path root =
            std::filesystem::temp_directory_path() /
            ("dsinfer-dictionary-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        DictionaryFile() {
            std::filesystem::create_directories(root);
        }
        ~DictionaryFile() {
            std::error_code ignored;
            std::filesystem::remove_all(root, ignored);
        }
        std::filesystem::path write(const std::string &text) {
            const auto path = root / "dictionary.txt";
            std::ofstream stream(path, std::ios::binary);
            stream << text;
            BOOST_REQUIRE(stream.good());
            return path;
        }
    };
}

BOOST_AUTO_TEST_CASE(lookups_and_both_iterator_directions_preserve_all_entries) {
    DictionaryFile file;
    const std::map<std::string, std::vector<std::string>> expected = {
        {"key1", {"val1", "val2"}                },
        {"key2", {"val3", "val4", "val5"}        },
        {"key3", {"val6", "val7", "val8", "val9"}}
    };
    ds::PhonemeDict dictionary;
    BOOST_CHECK(dictionary.empty());
    BOOST_CHECK(dictionary.begin() == dictionary.end());
    BOOST_REQUIRE(dictionary.load(
        file.write("key1\tval1 val2\r\nkey2\tval3 val4 val5\nkey3\tval6 val7 val8 val9"), nullptr));
    BOOST_CHECK_EQUAL(dictionary.size(), expected.size());
    BOOST_CHECK(!dictionary.empty());
    for (const auto &[key, values] : expected) {
        BOOST_CHECK(dictionary.contains(key.c_str()));
        const auto found = dictionary.find(key.c_str());
        BOOST_REQUIRE(found != dictionary.end());
        BOOST_CHECK_EQUAL(found->first, key);
        BOOST_CHECK(found->second.vec<std::string>() == values);
        BOOST_CHECK(dictionary[key.c_str()].vec<std::string>() == values);
        auto copied = found;
        BOOST_CHECK(copied == found);
        BOOST_CHECK((*copied).second.vec<std::string>() == values);
    }
    std::map<std::string, std::vector<std::string>> forward;
    for (auto it = dictionary.begin(); it != dictionary.end();) {
        auto previous = it++;
        forward.emplace(previous->first, previous->second.vec<std::string>());
    }
    BOOST_CHECK(forward == expected);
    std::map<std::string, std::vector<std::string>> backward;
    auto reverse = dictionary.end();
    while (reverse != dictionary.begin()) {
        --reverse;
        backward.emplace(reverse->first, reverse->second.vec<std::string>());
    }
    BOOST_CHECK(backward == expected);
    auto last = dictionary.end();
    const auto past = last--;
    BOOST_CHECK(past == dictionary.end());
    BOOST_CHECK(last != dictionary.end());
    BOOST_CHECK(expected.count(last->first) == 1u);
    for (const char *absent : {static_cast<const char *>(nullptr), "absent"}) {
        BOOST_CHECK(!dictionary.contains(absent));
        BOOST_CHECK(dictionary.find(absent) == dictionary.end());
        BOOST_CHECK(dictionary[absent].vec().empty());
    }
    ds::PhonemeList empty;
    BOOST_CHECK(empty.begin() == empty.end());
    BOOST_CHECK(empty.vec().empty());
    auto phones = dictionary["key1"];
    auto phone = phones.begin();
    const auto first = phone++;
    BOOST_CHECK_EQUAL(*first, "val1");
    BOOST_CHECK_EQUAL(*phone, "val2");
    ++phone;
    BOOST_CHECK(phone == phones.end());
}

BOOST_AUTO_TEST_CASE(failed_open_preserves_loaded_entries_and_successful_reload_replaces_them) {
    DictionaryFile file;
    ds::PhonemeDict dictionary;
    std::error_code error = std::make_error_code(std::errc::invalid_argument);
    BOOST_REQUIRE(dictionary.load(file.write("old\ta b\n"), &error));
    BOOST_CHECK(!error);
    BOOST_CHECK(!dictionary.load(file.root / "absent.txt", &error));
    BOOST_CHECK(static_cast<bool>(error));
    BOOST_CHECK(dictionary["old"].vec<std::string>() == std::vector<std::string>({"a", "b"}));
    BOOST_CHECK(!dictionary.load(file.root / "another-absent.txt", nullptr));
    BOOST_CHECK_EQUAL(dictionary.size(), 1u);
    BOOST_REQUIRE(dictionary.load(file.write("new\tc\n"), &error));
    BOOST_CHECK(!error);
    BOOST_CHECK(!dictionary.contains("old"));
    BOOST_CHECK(dictionary["new"].vec<std::string>() == std::vector<std::string>({"c"}));
    BOOST_REQUIRE(dictionary.load(file.write(""), &error));
    BOOST_CHECK(!error);
    BOOST_CHECK(dictionary.empty());
    BOOST_CHECK(dictionary.begin() == dictionary.end());
}

BOOST_AUTO_TEST_CASE(large_lexicons_reserve_and_iterate_without_losing_keys) {
    DictionaryFile file;
    const std::string phoneme(300, 'a');
    std::string text;
    for (int index = 0; index < 4096; ++index) {
        text += "word" + std::to_string(index) + "\t" + phoneme + " b\n";
    }
    BOOST_REQUIRE(text.size() > 1024 * 1024);
    ds::PhonemeDict dictionary;
    BOOST_REQUIRE(dictionary.load(file.write(text), nullptr));
    BOOST_CHECK_EQUAL(dictionary.size(), 4096u);
    for (const auto index : {0, 1, 2048, 4095}) {
        const auto key = "word" + std::to_string(index);
        BOOST_CHECK(dictionary[key.c_str()].vec<std::string>() ==
                    std::vector<std::string>({phoneme, "b"}));
    }
    std::size_t count = 0;
    for (const auto &entry : dictionary) {
        BOOST_CHECK_EQUAL(entry.second.vec().size(), 2u);
        ++count;
    }
    BOOST_CHECK_EQUAL(count, 4096u);
}
