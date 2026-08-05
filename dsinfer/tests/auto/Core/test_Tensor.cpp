#include <cstdint>
#include <cstring>
#include <vector>

#include <dsinfer/Core/Tensor.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

using ds::ITensor;
using ds::Tensor;
using ds::tensor_traits;

namespace {

    bool alignedTo(const void *p, std::size_t alignment) {
        return reinterpret_cast<std::uintptr_t>(p) % alignment == 0;
    }

}

BOOST_AUTO_TEST_SUITE(test_Tensor)

// Every data type the enumeration lists has to have a registered trait, or the templates silently
// refuse to work with it.
BOOST_AUTO_TEST_CASE(test_tensor_traits) {
    static_assert(tensor_traits<float>::is_valid);
    static_assert(tensor_traits<int64_t>::is_valid);
    static_assert(tensor_traits<bool>::is_valid);
    static_assert(tensor_traits<float>::data_type == ITensor::Float);
    static_assert(tensor_traits<int64_t>::data_type == ITensor::Int64);
    static_assert(tensor_traits<bool>::data_type == ITensor::Bool);

    // Anything else is not a tensor type, which is what the static_asserts in the templates rest
    // on.
    static_assert(!tensor_traits<double>::is_valid);
    static_assert(!tensor_traits<int>::is_valid);
    static_assert(tensor_traits<double>::data_type == ITensor::Undefined);
}

// A tensor that was never given a type answers rather than aborting. It is the state the default
// constructor leaves one in, so every accessor has to survive it.
BOOST_AUTO_TEST_CASE(test_Tensor_Default) {
    Tensor t;
    BOOST_CHECK(t.dataType() == ITensor::Undefined);
    BOOST_CHECK(t.shape().empty());
    BOOST_CHECK(t.byteSize() == 0);
    BOOST_CHECK(t.elementSize() == 0);
    BOOST_CHECK(t.elementCount() == 0);
    BOOST_CHECK(t.rawView().empty());
    BOOST_CHECK(t.backend() == Tensor::BACKEND);
}

BOOST_AUTO_TEST_CASE(test_Tensor_Create) {
    auto exp = Tensor::create(ITensor::Float, {2, 3});
    BOOST_REQUIRE(exp);

    auto t = exp.take();
    BOOST_CHECK(t->dataType() == ITensor::Float);
    BOOST_CHECK(t->shape() == std::vector<int64_t>({2, 3}));
    BOOST_CHECK(t->elementSize() == sizeof(float));
    BOOST_CHECK(t->elementCount() == 6);
    BOOST_CHECK(t->byteSize() == 6 * sizeof(float));

    // Documented as zero initialized, which a caller filling only part of it relies on.
    auto values = t->view<float>();
    BOOST_REQUIRE(values.size() == 6);
    for (float v : values) {
        BOOST_CHECK(v == 0.0f);
    }

    // An empty shape is a scalar, not an empty tensor.
    auto scalar = Tensor::create(ITensor::Int64, {});
    BOOST_REQUIRE(scalar);
    BOOST_CHECK(scalar.get()->elementCount() == 1);
    BOOST_CHECK(scalar.get()->shape().empty());
}

// A shape the tensor cannot be built from is an error rather than an abort or a wrong answer.
BOOST_AUTO_TEST_CASE(test_Tensor_CreateRejects) {
    BOOST_CHECK(!Tensor::create(ITensor::Undefined, {1}));
    BOOST_CHECK(!Tensor::create(ITensor::Undefined, {}));
    BOOST_CHECK(!Tensor::create(ITensor::Float, {0}));
    BOOST_CHECK(!Tensor::create(ITensor::Float, {2, 0, 3}));
    BOOST_CHECK(!Tensor::create(ITensor::Float, {-1}));

    // The product of the dimensions has to fit, and the check is on the byte count rather than the
    // element count, so a type wider than a byte overflows sooner.
    const auto huge = std::numeric_limits<int64_t>::max();
    BOOST_CHECK(!Tensor::create(ITensor::Float, {huge, huge}));
}

BOOST_AUTO_TEST_CASE(test_Tensor_CreateFromView) {
    const float source[] = {1.0f, 2.0f, 3.0f, 4.0f};

    auto exp = Tensor::createFromView<float>({2, 2}, stdc::array_view<float>(source, 4));
    BOOST_REQUIRE(exp);
    auto t = exp.take();

    BOOST_CHECK(t->dataType() == ITensor::Float);
    BOOST_CHECK(t->elementCount() == 4);
    BOOST_CHECK(t->view<float>()[3] == 4.0f);

    // The tensor holds its own copy, so the caller's buffer going away or changing does not reach
    // it.
    auto *mutableData = t->mutableData<float>();
    mutableData[0] = 99.0f;
    BOOST_CHECK(source[0] == 1.0f);

    // A view whose length disagrees with the shape is refused rather than read past.
    BOOST_CHECK(!Tensor::createFromView<float>({2, 3}, stdc::array_view<float>(source, 4)));
    BOOST_CHECK(!Tensor::createFromView<float>({2}, stdc::array_view<float>(source, 4)));
}

BOOST_AUTO_TEST_CASE(test_Tensor_CreateFromRawData) {
    Tensor::Container bytes(4 * sizeof(int64_t));
    auto *values = reinterpret_cast<int64_t *>(bytes.data());
    for (int i = 0; i < 4; ++i) {
        values[i] = i + 1;
    }

    // From a reference, which copies.
    {
        auto exp = Tensor::createFromRawData(ITensor::Int64, {4}, bytes);
        BOOST_REQUIRE(exp);
        auto t = exp.take();
        BOOST_CHECK(t->view<int64_t>()[2] == 3);
        BOOST_CHECK(bytes.size() == 4 * sizeof(int64_t));
    }
    // From an rvalue, which takes what it was given.
    {
        auto copy = bytes;
        auto exp = Tensor::createFromRawData(ITensor::Int64, {4}, std::move(copy));
        BOOST_REQUIRE(exp);
        BOOST_CHECK(exp.get()->view<int64_t>()[3] == 4);
    }
    // A byte count that does not match the shape is refused.
    BOOST_CHECK(!Tensor::createFromRawData(ITensor::Int64, {5}, bytes));
    BOOST_CHECK(!Tensor::createFromRawData(ITensor::Undefined, {4}, bytes));
}

BOOST_AUTO_TEST_CASE(test_Tensor_CreateScalar) {
    // As a one element vector.
    {
        auto exp = Tensor::createScalar<int64_t>(42);
        BOOST_REQUIRE(exp);
        auto t = exp.take();
        BOOST_CHECK(t->shape() == std::vector<int64_t>({1}));
        BOOST_CHECK(t->elementCount() == 1);
        BOOST_CHECK(t->view<int64_t>()[0] == 42);
    }
    // As a genuine scalar, which has no dimensions at all.
    {
        auto exp = Tensor::createScalar<float>(1.5f, true);
        BOOST_REQUIRE(exp);
        auto t = exp.take();
        BOOST_CHECK(t->shape().empty());
        BOOST_CHECK(t->elementCount() == 1);
        BOOST_CHECK(t->view<float>()[0] == 1.5f);
    }
}

BOOST_AUTO_TEST_CASE(test_Tensor_CreateFilled) {
    auto exp = Tensor::createFilled<float>({3, 2}, 7.5f);
    BOOST_REQUIRE(exp);
    auto t = exp.take();

    BOOST_CHECK(t->elementCount() == 6);
    for (float v : t->view<float>()) {
        BOOST_CHECK(v == 7.5f);
    }

    // A shape it cannot build carries the error out rather than filling nothing quietly.
    BOOST_CHECK(!Tensor::createFilled<float>({0}, 1.0f));
}

// Booleans are a byte each, which the templates assert and the layout depends on.
BOOST_AUTO_TEST_CASE(test_Tensor_Bool) {
    const bool source[] = {true, false, true};

    auto exp = Tensor::createFromView<bool>({3}, stdc::array_view<bool>(source, 3));
    BOOST_REQUIRE(exp);
    auto t = exp.take();

    BOOST_CHECK(t->dataType() == ITensor::Bool);
    BOOST_CHECK(t->elementSize() == 1);
    BOOST_CHECK(t->byteSize() == 3);

    auto values = t->view<bool>();
    BOOST_REQUIRE(values.size() == 3);
    BOOST_CHECK(values[0]);
    BOOST_CHECK(!values[1]);
    BOOST_CHECK(values[2]);
}

// Asking for the wrong type gives nothing rather than reinterpreting the bytes.
BOOST_AUTO_TEST_CASE(test_Tensor_TypedAccessChecksTheType) {
    auto exp = Tensor::createFilled<float>({4}, 1.0f);
    BOOST_REQUIRE(exp);
    auto t = exp.take();

    BOOST_CHECK(t->data<float>() != nullptr);
    BOOST_CHECK(t->mutableData<float>() != nullptr);
    BOOST_CHECK(!t->view<float>().empty());

    BOOST_CHECK(t->data<int64_t>() == nullptr);
    BOOST_CHECK(t->mutableData<int64_t>() == nullptr);
    BOOST_CHECK(t->view<int64_t>().empty());
    BOOST_CHECK(t->data<bool>() == nullptr);

    // The raw accessors do not care, which is what makes the typed ones worth having.
    BOOST_CHECK(t->rawData() != nullptr);
    BOOST_CHECK(t->rawView().size() == 4 * sizeof(float));
}

// The data sits where the aligned allocator was asked to put it.
BOOST_AUTO_TEST_CASE(test_Tensor_Alignment) {
    for (int64_t n : {int64_t(1), int64_t(3), int64_t(1000)}) {
        auto exp = Tensor::create(ITensor::Float, {n});
        BOOST_REQUIRE(exp);
        BOOST_CHECK(alignedTo(exp.get()->rawData(), Tensor::ALIGNMENT));
    }
}

BOOST_AUTO_TEST_CASE(test_Tensor_Clone) {
    auto exp = Tensor::createFilled<int64_t>({2, 2}, 5);
    BOOST_REQUIRE(exp);
    auto original = exp.take();

    auto copy = original->clone();
    BOOST_REQUIRE(copy != nullptr);
    BOOST_CHECK(copy.get() != original.get());
    BOOST_CHECK(copy->backend() == original->backend());
    BOOST_CHECK(copy->dataType() == original->dataType());
    BOOST_CHECK(copy->shape() == original->shape());
    BOOST_CHECK(copy->byteSize() == original->byteSize());

    // Deep, so writing through one is not visible through the other.
    original->mutableData<int64_t>()[0] = 99;
    BOOST_CHECK(copy->view<int64_t>()[0] == 5);
    BOOST_CHECK(original->view<int64_t>()[0] == 99);
}

// Moving takes the data across and leaves something that can still be asked questions.
BOOST_AUTO_TEST_CASE(test_Tensor_Move) {
    {
        Tensor source;
        {
            auto exp = Tensor::createFilled<float>({4}, 2.0f);
            BOOST_REQUIRE(exp);
            source = std::move(*exp.take());
        }
        BOOST_CHECK(source.dataType() == ITensor::Float);
        BOOST_CHECK(source.elementCount() == 4);

        Tensor moved(std::move(source));
        BOOST_CHECK(moved.dataType() == ITensor::Float);
        BOOST_CHECK(moved.view<float>()[0] == 2.0f);

        // What is left says it has no type, and answering that is the point.
        BOOST_CHECK(source.dataType() == ITensor::Undefined);
        BOOST_CHECK(source.elementSize() == 0);
        BOOST_CHECK(source.elementCount() == 0);
    }
    // Assigning to itself must not throw the data away.
    {
        auto exp = Tensor::createFilled<float>({2}, 3.0f);
        BOOST_REQUIRE(exp);
        auto t = exp.take();
        Tensor &alias = *t;
        alias = std::move(*t);
        BOOST_CHECK(t->elementCount() == 2);
        BOOST_CHECK(t->view<float>()[0] == 3.0f);
    }
}

// The interface is what everything downstream passes around, so the implementation has to be
// usable through it.
BOOST_AUTO_TEST_CASE(test_ITensor_ThroughTheInterface) {
    auto exp = Tensor::createFilled<int64_t>({3}, 8);
    BOOST_REQUIRE(exp);

    srt::NO<ITensor> tensor = exp.take();
    BOOST_CHECK(tensor->backend() == Tensor::BACKEND);
    BOOST_CHECK(tensor->dataType() == ITensor::Int64);
    BOOST_CHECK(tensor->elementCount() == 3);
    BOOST_CHECK(tensor->view<int64_t>()[1] == 8);
    BOOST_CHECK(tensor->clone()->view<int64_t>()[1] == 8);
}

BOOST_AUTO_TEST_SUITE_END()
