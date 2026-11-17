#include <map>

#include <onnxdriver/Session/SessionSystem.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_CASE(cache_key_orders_both_file_size_and_hash) {
    using Key = ds::onnxdriver::SessionSystem::HashSizeKey;
    Key first{100, {}}, same{100, {}}, otherHash{100, {}}, otherSize{101, {}};
    otherHash.hash[31] = 1;
    BOOST_CHECK(!(first < first));
    BOOST_CHECK(!(first < same));
    BOOST_CHECK(!(same < first));
    BOOST_CHECK((first < otherHash) != (otherHash < first));
    BOOST_CHECK((first < otherSize) != (otherSize < first));
    std::map<Key, int> values;
    values[first] = 1;
    values[same] = 2;
    values[otherHash] = 3;
    values[otherSize] = 4;
    BOOST_CHECK_EQUAL(values.size(), 3u);
    BOOST_CHECK_EQUAL(values.at(first), 2);
}
