#include <chrono>
#include <utility>
#include <fstream>

#include <stdcorelib/system.h>
#include <stdcorelib/console.h>
#include <stdcorelib/vla.h>
#include <stdcorelib/path.h>

#include <dsinfer/Support/PhonemeDict.h>

#include <boost/test/unit_test.hpp>

static void generateDictFile(const std::filesystem::path &filepath) {
    std::ofstream ofs(filepath, std::ios::binary);
    static const char content[] = "key1\tval1 val2\n"
                                  "key2\tval3 val4 val5\n"
                                  "key3\tval6 val7 val8 val9\n";
    ofs.write(content, sizeof(content) - 1);
    ofs.close();
}

BOOST_AUTO_TEST_SUITE(test_PhonemeDict)

BOOST_AUTO_TEST_CASE(test_DictFind) {
    std::filesystem::path filePath = stdc::system::application_directory() / "test_dict.txt";
    generateDictFile(filePath);

    {
        ds::PhonemeDict dict;
        BOOST_VERIFY(dict.load(filePath, nullptr));
        BOOST_CHECK(dict.size() == 3);

        auto it = dict.find("key1");
        BOOST_VERIFY(it != dict.end());
        BOOST_CHECK(it->second.vec() == std::vector<std::string_view>({"val1", "val2"}));

        it = dict.find("key2");
        BOOST_VERIFY(it != dict.end());
        BOOST_CHECK(it->second.vec() == std::vector<std::string_view>({"val3", "val4", "val5"}));

        it = dict.find("key3");
        BOOST_VERIFY(it != dict.end());
        BOOST_CHECK(it->second.vec() ==
                    std::vector<std::string_view>({"val6", "val7", "val8", "val9"}));
    }

    std::filesystem::remove(filePath);
}

BOOST_AUTO_TEST_SUITE_END()