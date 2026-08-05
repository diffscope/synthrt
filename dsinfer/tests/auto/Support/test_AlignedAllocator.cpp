#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <dsinfer/Support/AlignedAllocator.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

using ds::AlignedAllocator;

namespace {

    bool alignedTo(const void *p, std::size_t alignment) {
        return reinterpret_cast<std::uintptr_t>(p) % alignment == 0;
    }

}

BOOST_AUTO_TEST_SUITE(test_AlignedAllocator)

// The whole point: what comes back sits on the boundary that was asked for, which the default
// allocator promises only up to alignof(std::max_align_t).
BOOST_AUTO_TEST_CASE(test_AlignedAllocator_Alignment) {
    {
        AlignedAllocator<float, 64> alloc;
        for (std::size_t n : {std::size_t(1), std::size_t(3), std::size_t(1000)}) {
            float *p = alloc.allocate(n);
            BOOST_REQUIRE(p != nullptr);
            BOOST_CHECK(alignedTo(p, 64));
            alloc.deallocate(p, n);
        }
    }
    {
        AlignedAllocator<std::uint8_t, 4096> alloc;
        auto *p = alloc.allocate(1);
        BOOST_REQUIRE(p != nullptr);
        BOOST_CHECK(alignedTo(p, 4096));
        alloc.deallocate(p, 1);
    }
}

// Allocating nothing is allowed, and whatever comes back is safe to hand straight back.
BOOST_AUTO_TEST_CASE(test_AlignedAllocator_Zero) {
    AlignedAllocator<int, 32> alloc;
    int *p = alloc.allocate(0);
    alloc.deallocate(p, 0);
}

// A request that would overflow the byte count is refused rather than wrapping around and
// returning a block far too small for it.
BOOST_AUTO_TEST_CASE(test_AlignedAllocator_TooLarge) {
    AlignedAllocator<double, 32> alloc;
    BOOST_CHECK(alloc.max_size() == std::numeric_limits<std::size_t>::max() / sizeof(double));
    BOOST_CHECK_THROW(alloc.allocate(alloc.max_size() + 1), std::bad_alloc);
}

// Two allocators of the same alignment are interchangeable, which is what lets one container's
// memory be freed by another's allocator.
BOOST_AUTO_TEST_CASE(test_AlignedAllocator_Equality) {
    BOOST_CHECK((AlignedAllocator<float, 64>{} == AlignedAllocator<float, 64>{}));
    BOOST_CHECK((AlignedAllocator<float, 64>{} == AlignedAllocator<double, 64>{}));
    BOOST_CHECK((AlignedAllocator<float, 64>{} != AlignedAllocator<float, 32>{}));

    static_assert(std::is_same_v<AlignedAllocator<float, 64>::rebind<double>::other,
                                 AlignedAllocator<double, 64>>);

    // The converting constructor is what a container uses when it rebinds for its node type.
    AlignedAllocator<double, 64> fromOther{AlignedAllocator<float, 64>{}};
    BOOST_CHECK((fromOther == AlignedAllocator<double, 64>{}));
}

// Standing in for the real use, which is a vector of samples an audio or inference back end wants
// aligned.
BOOST_AUTO_TEST_CASE(test_AlignedAllocator_InAContainer) {
    std::vector<float, AlignedAllocator<float, 64>> v;
    for (int i = 0; i < 1000; ++i) {
        v.push_back(float(i));
        // Every reallocation has to land on the boundary too, not just the first block.
        BOOST_REQUIRE(alignedTo(v.data(), 64));
    }
    BOOST_CHECK(v.size() == 1000);
    BOOST_CHECK(v.front() == 0.0f);
    BOOST_CHECK(v.back() == 999.0f);

    auto copy = v;
    BOOST_CHECK(alignedTo(copy.data(), 64));
    BOOST_CHECK(copy == v);
}

BOOST_AUTO_TEST_SUITE_END()
