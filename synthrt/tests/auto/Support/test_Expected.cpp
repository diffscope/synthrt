#include <memory>
#include <string>

#include <synthrt/Support/Expected.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

using srt::Error;
using srt::Expected;

namespace {

    /// Counts its own lifetime, so a test can show the contained value is destroyed exactly once
    /// across the placement-new and manual-destructor juggling the union storage requires.
    struct Tracked {
        static int alive;
        static int destroyed;

        int value;

        explicit Tracked(int v = 0) : value(v) {
            alive++;
        }
        Tracked(const Tracked &RHS) : value(RHS.value) {
            alive++;
        }
        Tracked(Tracked &&RHS) noexcept : value(RHS.value) {
            RHS.value = -1;
            alive++;
        }
        ~Tracked() {
            alive--;
            destroyed++;
        }

        static void reset() {
            alive = 0;
            destroyed = 0;
        }
    };

    int Tracked::alive = 0;
    int Tracked::destroyed = 0;

}

BOOST_AUTO_TEST_SUITE(test_Expected)

BOOST_AUTO_TEST_CASE(test_Expected) {
    // Construct from error
    {
        Expected<std::string> e((Error(Error::InvalidArgument)));
        BOOST_CHECK(!e.hasValue());
        BOOST_CHECK(e.error().type() == Error::InvalidArgument);
        BOOST_CHECK(e.error().message() == Error(Error::InvalidArgument).message());
    }
    // Construct from value
    {
        Expected<std::string> e("hello");
        BOOST_CHECK(e.hasValue());
        BOOST_CHECK(e.get() == "hello");
    }
    // Construct from convertible Expected
    {
        Expected<std::string> e1 = Expected<const char *>(Error(Error::InvalidArgument));
        BOOST_CHECK(!e1.hasValue());
        BOOST_CHECK(e1.error().type() == Error::InvalidArgument);

        Expected<std::string> e2 = Expected<const char *>("hello");
        BOOST_CHECK(e2.hasValue());
        BOOST_CHECK(e2.get() == "hello");
    }
}

// operator bool() used to be non-const, so a const Expected could not be tested at all.
BOOST_AUTO_TEST_CASE(test_Expected_ConstContextualConversion) {
    const Expected<std::string> value("hello");
    const Expected<std::string> error((Error(Error::InvalidArgument)));

    BOOST_CHECK(static_cast<bool>(value));
    BOOST_CHECK(!static_cast<bool>(error));

    // The shape that failed to compile before: binding to a const reference and testing it.
    const auto &ref = value;
    BOOST_CHECK(ref ? true : false);

    const Expected<void> voidValue;
    const Expected<void> voidError((Error(Error::InvalidArgument)));
    BOOST_CHECK(static_cast<bool>(voidValue));
    BOOST_CHECK(!static_cast<bool>(voidError));
}

// \c valueOr's const overload took <tt>const U &</tt> and then applied \c std::forward, which
// casts away constness. The overload never compiled, so nothing ever instantiated it.
BOOST_AUTO_TEST_CASE(test_Expected_ValueOr) {
    {
        const Expected<std::string> value("hello");
        const Expected<std::string> error((Error(Error::InvalidArgument)));

        BOOST_CHECK(value.valueOr("fallback") == "hello");
        BOOST_CHECK(error.valueOr("fallback") == "fallback");

        // A named lvalue is the case that used to fail.
        const std::string fallback = "fallback";
        BOOST_CHECK(value.valueOr(fallback) == "hello");
        BOOST_CHECK(error.valueOr(fallback) == "fallback");
    }
    // Rvalue overload moves out of the contained value.
    {
        BOOST_CHECK(Expected<std::string>("hello").valueOr("fallback") == "hello");
        BOOST_CHECK(Expected<std::string>(Error(Error::InvalidArgument)).valueOr("fallback") ==
                    "fallback");
    }
}

BOOST_AUTO_TEST_CASE(test_Expected_WithContext) {
    // A value passes through untouched.
    {
        auto e = Expected<std::string>("hello").withContext(Error::FileNotOpen, "outer");
        BOOST_CHECK(e.hasValue());
        BOOST_CHECK(e.get() == "hello");
    }
    // An error gets wrapped, and the original becomes the cause.
    {
        auto e = Expected<std::string>(Error(Error::InvalidFormat, "inner"))
                     .withContext(Error::FileNotOpen, "outer");
        BOOST_CHECK(!e.hasValue());
        BOOST_CHECK(e.error().type() == Error::FileNotOpen);
        BOOST_CHECK(e.error().message() == "outer");
        BOOST_CHECK(e.error().toString() == "outer: inner");
        BOOST_CHECK(e.error().rootCause().type() == Error::InvalidFormat);
    }
    // Stacking keeps every level.
    {
        auto e = Expected<std::string>(Error(Error::InvalidArgument, "root"))
                     .withContext(Error::InvalidFormat, "middle")
                     .withContext(Error::FileNotOpen, "outer");
        BOOST_CHECK(e.error().toString() == "outer: middle: root");
        BOOST_CHECK(e.error().rootCause().type() == Error::InvalidArgument);
    }
    // Expected<void> behaves the same.
    {
        auto ok = Expected<void>().withContext(Error::FileNotOpen, "outer");
        BOOST_CHECK(ok.hasValue());

        auto e = Expected<void>(Error(Error::InvalidFormat, "inner"))
                     .withContext(Error::FileNotOpen, "outer");
        BOOST_CHECK(!e.hasValue());
        BOOST_CHECK(e.error().toString() == "outer: inner");
    }
}

BOOST_AUTO_TEST_CASE(test_Expected_MoveAssign) {
    // Every combination of source and destination state, including the ones that change state.
    {
        Expected<std::string> e("hello");
        e = Expected<std::string>("world");
        BOOST_CHECK(e.hasValue());
        BOOST_CHECK(e.get() == "world");
    }
    {
        Expected<std::string> e("hello");
        e = Expected<std::string>(Error(Error::NotImplemented));
        BOOST_CHECK(!e.hasValue());
        BOOST_CHECK(e.error().type() == Error::NotImplemented);
    }
    {
        Expected<std::string> e((Error(Error::InvalidArgument)));
        e = Expected<std::string>("hello");
        BOOST_CHECK(e.hasValue());
        BOOST_CHECK(e.get() == "hello");
    }
    {
        Expected<std::string> e((Error(Error::InvalidArgument)));
        e = Expected<std::string>(Error(Error::NotImplemented));
        BOOST_CHECK(!e.hasValue());
        BOOST_CHECK(e.error().type() == Error::NotImplemented);
    }
    {
        Expected<void> e;
        e = Expected<void>(Error(Error::NotImplemented));
        BOOST_CHECK(!e.hasValue());
        BOOST_CHECK(e.error().type() == Error::NotImplemented);
    }
}

BOOST_AUTO_TEST_CASE(test_Expected_Void) {
    // Construct from error
    {
        Expected<void> e((Error(Error::InvalidArgument)));
        BOOST_CHECK(!e.hasValue());
        BOOST_CHECK(e.error().type() == Error::InvalidArgument);
        BOOST_CHECK(e.error().message() == Error(Error::InvalidArgument).message());
    }
    // Construct from convertible Expected
    {
        Expected<void> e1 = Expected<const char *>(Error(Error::InvalidArgument));
        BOOST_CHECK(!e1.hasValue());
        BOOST_CHECK(e1.error().type() == Error::InvalidArgument);

        Expected<void> e2 = Expected<const char *>("hello");
        BOOST_CHECK(e2.hasValue());
    }
    // The default constructor is the success case.
    {
        Expected<void> e;
        BOOST_CHECK(e.hasValue());
        BOOST_CHECK(static_cast<bool>(e));
    }
    // A value-carrying Expected collapses to void, dropping the value.
    {
        Expected<void> e = Expected<std::string>("discarded");
        BOOST_CHECK(e.hasValue());
    }
}

// The default constructor of the primary template value-initializes T, which is easy to mistake for
// an empty or error state.
BOOST_AUTO_TEST_CASE(test_Expected_DefaultIsAValue) {
    Expected<std::string> s;
    BOOST_CHECK(s.hasValue());
    BOOST_CHECK(s.get().empty());

    Expected<int> i;
    BOOST_CHECK(i.hasValue());
    BOOST_CHECK(i.get() == 0);
}

BOOST_AUTO_TEST_CASE(test_Expected_Accessors) {
    // get(), value(), operator* and operator-> are all the same thing.
    {
        Expected<std::string> e("hello");
        BOOST_CHECK(e.get() == "hello");
        BOOST_CHECK(e.value() == "hello");
        BOOST_CHECK(*e == "hello");
        BOOST_CHECK(e->size() == 5);

        // Writing through the mutable reference is visible to the others.
        e.get() += " world";
        BOOST_CHECK(*e == "hello world");
    }
    // The const overloads give read-only access to the same object.
    {
        const Expected<std::string> e("hello");
        BOOST_CHECK(e.get() == "hello");
        BOOST_CHECK(e.value() == "hello");
        BOOST_CHECK(*e == "hello");
        BOOST_CHECK(e->size() == 5);
        BOOST_CHECK(&e.get() == &*e);
    }
    // take() moves the value out.
    {
        Expected<std::string> e("hello");
        std::string taken = e.take();
        BOOST_CHECK(taken == "hello");
    }
    // takeError() moves the error out, and answers success when there is a value.
    {
        Expected<std::string> e((Error(Error::InvalidArgument, "bad")));
        Error err = e.takeError();
        BOOST_CHECK(!err.ok());
        BOOST_CHECK(err.message() == "bad");

        Expected<std::string> ok("hello");
        BOOST_CHECK(ok.takeError().ok());

        Expected<void> voidError((Error(Error::NotImplemented)));
        BOOST_CHECK(voidError.takeError().type() == Error::NotImplemented);
        BOOST_CHECK(Expected<void>().takeError().ok());
    }
    // The whole error, cause chain included, survives being stored.
    {
        Expected<std::string> e(Error(Error::FileNotOpen, "outer")
                                    .withCause(Error(Error::InvalidFormat, "inner")));
        BOOST_CHECK(e.error().toString() == "outer: inner");
        BOOST_CHECK(e.error().rootCause().type() == Error::InvalidFormat);
    }
}

// A move-only T must work, since that is what the codebase mostly stores.
BOOST_AUTO_TEST_CASE(test_Expected_MoveOnlyValue) {
    {
        Expected<std::unique_ptr<int>> e(std::make_unique<int>(42));
        BOOST_REQUIRE(e.hasValue());
        BOOST_CHECK(*e.get() == 42);

        std::unique_ptr<int> taken = e.take();
        BOOST_REQUIRE(taken);
        BOOST_CHECK(*taken == 42);
    }
    // Move construction, move assignment and the error path all hold up.
    {
        Expected<std::unique_ptr<int>> source(std::make_unique<int>(7));
        Expected<std::unique_ptr<int>> moved(std::move(source));
        BOOST_REQUIRE(moved.hasValue());
        BOOST_CHECK(**moved == 7);

        moved = Expected<std::unique_ptr<int>>(Error(Error::NotImplemented));
        BOOST_CHECK(!moved.hasValue());
        BOOST_CHECK(moved.error().type() == Error::NotImplemented);
    }
    // withContext() carries a move-only value through untouched.
    {
        auto e = Expected<std::unique_ptr<int>>(std::make_unique<int>(3))
                     .withContext(Error::FileNotOpen, "outer");
        BOOST_REQUIRE(e.hasValue());
        BOOST_CHECK(**e == 3);
    }
}

// The union storage is built and torn down by hand, so the contained value must be destroyed once
// per construction and no more.
BOOST_AUTO_TEST_CASE(test_Expected_ValueLifetime) {
    // Plain construction and destruction.
    {
        Tracked::reset();
        {
            Expected<Tracked> e{Tracked(1)};
            BOOST_CHECK(e.get().value == 1);
        }
        BOOST_CHECK(Tracked::alive == 0);
    }
    // An error path never builds a T at all.
    {
        Tracked::reset();
        {
            Expected<Tracked> e((Error(Error::InvalidArgument)));
            BOOST_CHECK(!e.hasValue());
        }
        BOOST_CHECK(Tracked::alive == 0);
        BOOST_CHECK(Tracked::destroyed == 0);
    }
    // Move assignment tears the old value down before building the new one.
    {
        Tracked::reset();
        {
            Expected<Tracked> e{Tracked(1)};
            e = Expected<Tracked>(Tracked(2));
            BOOST_CHECK(e.get().value == 2);
        }
        BOOST_CHECK(Tracked::alive == 0);
    }
    // Changing from a value to an error destroys the value.
    {
        Tracked::reset();
        {
            Expected<Tracked> e{Tracked(1)};
            e = Expected<Tracked>(Error(Error::NotImplemented));
            BOOST_CHECK(!e.hasValue());
            BOOST_CHECK(Tracked::alive == 0);
        }
        BOOST_CHECK(Tracked::alive == 0);
    }
    // Self move assignment is a no-op rather than a self-destruction.
    {
        Tracked::reset();
        {
            Expected<Tracked> e{Tracked(5)};
            e = std::move(e);
            BOOST_REQUIRE(e.hasValue());
            BOOST_CHECK(e.get().value == 5);
        }
        BOOST_CHECK(Tracked::alive == 0);
    }
}

BOOST_AUTO_TEST_SUITE_END()