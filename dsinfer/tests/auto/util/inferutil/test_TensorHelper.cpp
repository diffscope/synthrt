#include <cstdint>
#include <utility>

#include <inferutil/TensorHelper.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_CASE(bounded_writes_and_move_preserve_cursor) {
    auto result = ds::inferutil::TensorHelper<int64_t>::createFor1DArray(3);
    BOOST_REQUIRE(result);
    auto writer = result.take();
    BOOST_CHECK(!writer.isComplete());
    BOOST_CHECK(writer.write(11));
    auto moved = std::move(writer);
    BOOST_CHECK(moved.write(22));
    moved.writeUnchecked(33);
    BOOST_CHECK(moved.isComplete());
    BOOST_CHECK(!moved.write(99));
    auto tensor = moved.take();
    BOOST_REQUIRE(tensor);
    BOOST_CHECK_EQUAL(tensor->data<int64_t>()[0], 11);
    BOOST_CHECK_EQUAL(tensor->data<int64_t>()[1], 22);
    BOOST_CHECK_EQUAL(tensor->data<int64_t>()[2], 33);
}

BOOST_AUTO_TEST_CASE(move_assignment_replaces_storage_and_cursor) {
    auto first = ds::inferutil::TensorHelper<float>::createFor1DArray(2);
    auto second = ds::inferutil::TensorHelper<float>::createFor1DArray(1);
    BOOST_REQUIRE(first);
    BOOST_REQUIRE(second);
    auto source = first.take();
    auto target = second.take();
    BOOST_REQUIRE(source.write(0.5f));
    target = std::move(source);
    BOOST_REQUIRE(target.write(1.5f));
    BOOST_CHECK(target.isComplete());
    BOOST_CHECK_EQUAL(target.value()->data<float>()[0], 0.5f);
    BOOST_CHECK_EQUAL(target.value()->data<float>()[1], 1.5f);
}
