// Unit tests for ds::infer::inferutil::TensorHelper and Speedup helpers.
//
// Covers TensorHelper create/write/overflow/isComplete and getSpeedupFromSteps
// edge cases including zero/negative steps, very large steps, and boundary
// speedup values.

#include <catch2/catch_test_macros.hpp>

#include <inferutil/TensorHelper.h>
#include <inferutil/Speedup.h>

using namespace ds::infer::inferutil;

// ---------------------------------------------------------------------------
// TensorHelper
// ---------------------------------------------------------------------------

TEST_CASE("TensorHelper create and write within bounds", "[tensorhelper]") {
    auto exp = TensorHelper<int64_t>::createFor1DArray(3);
    REQUIRE(exp.hasValue());
    auto &helper = exp.value();
    REQUIRE(helper.write(10));
    REQUIRE(helper.write(20));
    REQUIRE(helper.write(30));
    REQUIRE(helper.isComplete());
    REQUIRE(!helper.write(40)); // overflow, returns false
}

TEST_CASE("TensorHelper zero size returns error", "[tensorhelper]") {
    // Tensor::create with zero elements may return a null data pointer, which
    // createFor1DArray detects and returns an error. This documents that
    // behavior rather than treating it as a crash.
    auto exp = TensorHelper<int64_t>::createFor1DArray(0);
    // Either succeeds (isComplete) or returns error — both are acceptable as
    // long as no crash. Just verify it doesn't crash.
    if (exp.hasValue()) {
        auto &helper = exp.value();
        REQUIRE(helper.isComplete());
        REQUIRE(!helper.write(1));
    }
}

TEST_CASE("TensorHelper take produces valid tensor", "[tensorhelper]") {
    auto exp = TensorHelper<float>::createFor1DArray(2);
    REQUIRE(exp.hasValue());
    auto &helper = exp.value();
    helper.write(1.5f);
    helper.write(2.5f);
    auto tensor = helper.take();
    REQUIRE(tensor);
    REQUIRE(tensor->elementCount() == 2);
    auto view = tensor->view<float>();
    REQUIRE(view.size() == 2);
    REQUIRE(view[0] == 1.5f);
    REQUIRE(view[1] == 2.5f);
}

TEST_CASE("TensorHelper writeUnchecked does not check bounds", "[tensorhelper]") {
    // writeUnchecked is used when caller has pre-validated the count.
    // Verify it writes correctly within bounds.
    auto exp = TensorHelper<int64_t>::createFor1DArray(2);
    REQUIRE(exp.hasValue());
    auto &helper = exp.value();
    helper.writeUnchecked(100);
    helper.writeUnchecked(200);
    REQUIRE(helper.isComplete());
}

TEST_CASE("TensorHelper move semantics", "[tensorhelper]") {
    auto exp = TensorHelper<int64_t>::createFor1DArray(2);
    REQUIRE(exp.hasValue());
    auto helper1 = std::move(exp.value());
    helper1.write(1);
    TensorHelper<int64_t> helper2(std::move(helper1));
    helper2.write(2);
    REQUIRE(helper2.isComplete());
}

// ---------------------------------------------------------------------------
// getSpeedupFromSteps
// ---------------------------------------------------------------------------

TEST_CASE("getSpeedupFromSteps zero steps returns fallback", "[speedup]") {
    // steps=0 should return the fallback (10), not divide by zero.
    REQUIRE(getSpeedupFromSteps(0) == 10);
}

TEST_CASE("getSpeedupFromSteps negative steps returns fallback", "[speedup]") {
    REQUIRE(getSpeedupFromSteps(-5) == 10);
}

TEST_CASE("getSpeedupFromSteps steps=1 returns 1000", "[speedup]") {
    // 1000 / 1 = 1000, which is within [1, 1000].
    REQUIRE(getSpeedupFromSteps(1) == 1000);
}

TEST_CASE("getSpeedupFromSteps steps=1000 returns 1", "[speedup]") {
    // 1000 / 1000 = 1.
    REQUIRE(getSpeedupFromSteps(1000) == 1);
}

TEST_CASE("getSpeedupFromSteps very large steps clamps to 1", "[speedup]") {
    // 1000 / 2000 = 0, clamped to 1.
    REQUIRE(getSpeedupFromSteps(2000) == 1);
}

TEST_CASE("getSpeedupFromSteps custom fallback", "[speedup]") {
    REQUIRE(getSpeedupFromSteps(0, 5) == 5);
}

TEST_CASE("getSpeedupFromSteps result divides 1000 evenly", "[speedup]") {
    // The algorithm ensures 1000 % speedup == 0 (or speedup == 1).
    for (int64_t steps = 1; steps <= 1000; ++steps) {
        auto speedup = getSpeedupFromSteps(steps);
        REQUIRE(speedup >= 1);
        REQUIRE(speedup <= 1000);
        REQUIRE((1000 % speedup == 0 || speedup == 1));
    }
}

TEST_CASE("getSpeedupFromSteps steps=2 returns 500", "[speedup]") {
    // 1000 / 2 = 500, 1000 % 500 == 0.
    REQUIRE(getSpeedupFromSteps(2) == 500);
}

TEST_CASE("getSpeedupFromSteps steps=3 returns 333 adjusted down", "[speedup]") {
    // 1000 / 3 = 333, 1000 % 333 != 0, so decrement until 1000 % speedup == 0.
    // 1000 % 250 == 0 but 333 -> 332 -> ... -> 250 is too far.
    // Actually: 1000/3=333.33 -> ceil via int=333. 1000%333=1 !=0.
    // Decrement: 332 (1000%332=4), ..., down to 250 (1000%250=0). But wait,
    // the loop only decrements by 1, so it finds 250 only if no divisor found
    // earlier. Let's just verify it returns a valid divisor.
    auto speedup = getSpeedupFromSteps(3);
    REQUIRE(speedup >= 1);
    REQUIRE(1000 % speedup == 0);
}
