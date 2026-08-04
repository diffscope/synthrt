#include <synthrt/Support/Error.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

using srt::Error;

namespace {

    /// Stands in for a downstream library registering a domain of its own, which is what dsinfer
    /// and third-party plugins do rather than reaching for a code of synthrt's.
    enum class TestErrc {
        Broken = 100,

        /// Deliberately shares a numeric value with \c Error::InvalidFormat, so that a test can
        /// show the two do not collide.
        Overlapping = static_cast<int>(Error::InvalidFormat),
    };

    class TestCategory : public std::error_category {
    public:
        const char *name() const noexcept override {
            return "srt.test";
        }
        std::string message(int code) const override {
            switch (static_cast<TestErrc>(code)) {
                case TestErrc::Broken:
                    return "broken";
                case TestErrc::Overlapping:
                    return "overlapping";
                default:
                    return "unknown";
            }
        }
    };

    const std::error_category &testCategory() noexcept {
        static TestCategory instance;
        return instance;
    }

    std::error_code make_error_code(TestErrc code) noexcept {
        return {static_cast<int>(code), testCategory()};
    }

}

template <>
struct std::is_error_code_enum<TestErrc> : std::true_type {};

BOOST_AUTO_TEST_SUITE(test_Error)

BOOST_AUTO_TEST_CASE(test_Error_Basic) {
    {
        Error e;
        BOOST_CHECK(e.ok());
        BOOST_CHECK(e.type() == Error::NoError);
    }
    {
        Error e(Error::InvalidFormat);
        BOOST_CHECK(!e.ok());
        BOOST_CHECK(e.message() == "invalid format");
    }
    {
        Error e(Error::InvalidFormat, "custom");
        BOOST_CHECK(e.message() == "custom");
        BOOST_CHECK(e.type() == Error::InvalidFormat);
    }
    // A null message must not be handed to std::string.
    {
        Error e(Error::InvalidFormat, static_cast<const char *>(nullptr));
        BOOST_CHECK(e.message().empty());
    }
    // An empty message is kept as such rather than falling back to the canned text.
    {
        Error e(Error::InvalidFormat, std::string());
        BOOST_CHECK(e.message().empty());
        BOOST_CHECK(!e.ok());
    }
    // success() and the default constructor agree.
    {
        BOOST_CHECK(Error::success().ok());
        BOOST_CHECK(Error::success().code() == Error::NoError);
        BOOST_CHECK(!static_cast<bool>(Error::success().code()));
        BOOST_CHECK(Error::success().message().empty());
    }
    // what() gives the same text as message().
    {
        Error e(Error::FileNotFound, "gone");
        BOOST_CHECK(std::string(e.what()) == e.message());
    }
    // Every enumerator has canned text of its own, and none of them is empty.
    {
        const Error::ErrorCode codes[] = {
            Error::InvalidFormat,       Error::FileNotFound,        Error::FileNotOpen,
            Error::FileDuplicated,      Error::RecursiveDependency, Error::FeatureNotSupported,
            Error::InvalidArgument,     Error::NotImplemented,
        };
        for (auto code : codes) {
            Error e(code);
            BOOST_CHECK(!e.ok());
            BOOST_CHECK(!e.message().empty());
            BOOST_CHECK(e.type() == static_cast<int>(code));
        }
    }
    // The int constructor is the same thing without the enum.
    {
        Error e(static_cast<int>(Error::FileNotFound));
        BOOST_CHECK(e.code() == Error::FileNotFound);
        BOOST_CHECK(e.message() == Error(Error::FileNotFound).message());
    }
}

// An \c ErrorCode converts to \c std::error_code implicitly, which is what makes comparing against
// code() read the way it does. It only works while the \c std::is_error_code_enum specialization is
// seen before anything inside the class needs it, so this is the guard against that regressing.
BOOST_AUTO_TEST_CASE(test_Error_CodeConversion) {
    static_assert(std::is_error_code_enum_v<Error::ErrorCode>,
                  "ErrorCode lost its implicit conversion to std::error_code");

    Error e(Error::FileNotFound, "gone");
    BOOST_CHECK(e.code() == Error::FileNotFound);
    BOOST_CHECK(e.code() != Error::FileNotOpen);
    BOOST_CHECK(e.code().category() == Error::category());
    BOOST_CHECK(std::string(Error::category().name()) == "srt");

    // A comparison written without naming Error at all still resolves through the category.
    std::error_code ec = Error::FileNotFound;
    BOOST_CHECK(ec == e.code());
}

// The point of carrying a \c std::error_code rather than a bare int: a library downstream can raise
// its own errors without their values running into synthrt's.
BOOST_AUTO_TEST_CASE(test_Error_ForeignDomain) {
    // Constructed from a foreign code, the text comes from that code's own category.
    {
        Error e(make_error_code(TestErrc::Broken));
        BOOST_CHECK(!e.ok());
        BOOST_CHECK(e.message() == "broken");
        BOOST_CHECK(e.code() == TestErrc::Broken);
        BOOST_CHECK(e.code().category() != Error::category());
    }
    // An explicit message wins over the category's.
    {
        Error e(make_error_code(TestErrc::Broken), "something specific");
        BOOST_CHECK(e.message() == "something specific");
        BOOST_CHECK(e.code() == TestErrc::Broken);
    }
    // Same numeric value, different domain: code() tells them apart, type() cannot.
    {
        Error foreign(make_error_code(TestErrc::Overlapping));
        Error own(Error::InvalidFormat);

        BOOST_CHECK(foreign.type() == own.type()); // why type() is not enough on its own
        BOOST_CHECK(foreign.code() != own.code());
        BOOST_CHECK(foreign.code() != Error::InvalidFormat);
        BOOST_CHECK(own.code() == Error::InvalidFormat);
        BOOST_CHECK(foreign.message() == "overlapping");
    }
    // Domains mix freely within one chain, and the root keeps its own.
    {
        Error e = Error(Error::FileNotOpen, "cannot load plugin")
                      .withCause(Error(make_error_code(TestErrc::Broken), "device lost"));

        BOOST_CHECK(e.code() == Error::FileNotOpen);
        BOOST_CHECK(e.rootCause().code() == TestErrc::Broken);
        BOOST_CHECK(e.rootCause().code().category() == testCategory());
        BOOST_CHECK(e.toString() == "cannot load plugin: device lost");
    }
    // A generic code from the standard library works just as well.
    {
        Error e(std::make_error_code(std::errc::no_such_file_or_directory));
        BOOST_CHECK(!e.ok());
        BOOST_CHECK(!e.message().empty());
        BOOST_CHECK(e.code() == std::errc::no_such_file_or_directory);
    }
}

BOOST_AUTO_TEST_CASE(test_Error_Cause) {
    // A root error has no cause and renders as its own message.
    {
        Error root(Error::InvalidFormat, "unexpected token");
        BOOST_CHECK(root.cause().ok());
        BOOST_CHECK(root.toString() == "unexpected token");
        BOOST_CHECK(root.rootCause().message() == "unexpected token");
    }
    // One level of chaining.
    {
        Error root(Error::InvalidFormat, "unexpected token");
        Error wrapped = Error(Error::FileNotOpen, "cannot read desc.json").withCause(root);

        BOOST_CHECK(wrapped.type() == Error::FileNotOpen);
        BOOST_CHECK(wrapped.message() == "cannot read desc.json");
        BOOST_CHECK(wrapped.toString() == "cannot read desc.json: unexpected token");

        BOOST_CHECK(wrapped.cause().type() == Error::InvalidFormat);
        BOOST_CHECK(wrapped.rootCause().type() == Error::InvalidFormat);
    }
    // Several levels. The root type survives, which it does not when messages are spliced.
    {
        Error e = Error(Error::FileNotOpen, "outer")
                      .withCause(Error(Error::FileNotFound, "middle")
                                     .withCause(Error(Error::InvalidArgument, "inner")));

        BOOST_CHECK(e.toString() == "outer: middle: inner");
        BOOST_CHECK(e.rootCause().type() == Error::InvalidArgument);
        BOOST_CHECK(e.cause().cause().message() == "inner");
    }
    // A successful cause is dropped, so a call that may or may not have failed can be passed in.
    {
        Error e = Error(Error::FileNotOpen, "outer").withCause(Error::success());
        BOOST_CHECK(e.cause().ok());
        BOOST_CHECK(e.toString() == "outer");
    }
    // Levels without text of their own do not leave stray separators behind.
    {
        Error e = Error(Error::FileNotOpen, "").withCause(Error(Error::InvalidFormat, "inner"));
        BOOST_CHECK(e.toString() == "inner");
    }
    // \c withCause() returns a copy, leaving the original untouched.
    {
        Error original(Error::FileNotOpen, "outer");
        Error chained = original.withCause(Error(Error::InvalidFormat, "inner"));

        BOOST_CHECK(original.cause().ok());
        BOOST_CHECK(original.toString() == "outer");
        BOOST_CHECK(chained.toString() == "outer: inner");
    }
    // Copies share the chain rather than duplicating it.
    {
        Error e = Error(Error::FileNotOpen, "outer").withCause(Error(Error::InvalidFormat, "inner"));
        Error copy = e;
        BOOST_CHECK(copy.toString() == e.toString());
        BOOST_CHECK(copy.rootCause().type() == Error::InvalidFormat);
    }
    // A cause taken out of the chain keeps everything below it.
    {
        Error e = Error(Error::FileNotOpen, "a")
                      .withCause(Error(Error::FileNotFound, "b")
                                     .withCause(Error(Error::InvalidArgument, "c")));
        Error middle = e.cause();
        BOOST_CHECK(middle.toString() == "b: c");
        BOOST_CHECK(middle.rootCause().type() == Error::InvalidArgument);
    }
    // Applying withCause() again replaces the cause rather than appending to it.
    {
        Error e = Error(Error::FileNotOpen, "outer")
                      .withCause(Error(Error::InvalidFormat, "first"))
                      .withCause(Error(Error::InvalidArgument, "second"));
        BOOST_CHECK(e.toString() == "outer: second");
        BOOST_CHECK(e.rootCause().type() == Error::InvalidArgument);
    }
    // A chain of successful errors renders as nothing at all.
    {
        Error e = Error(Error::FileNotOpen, "").withCause(Error(Error::InvalidFormat, ""));
        BOOST_CHECK(e.toString().empty());
        BOOST_CHECK(!e.ok()); // still an error, it just carries no text
    }
    // Depth is not special-cased anywhere, so a long chain must behave like a short one.
    {
        Error e(Error::InvalidArgument, "0");
        for (int i = 1; i < 32; ++i) {
            e = Error(Error::FileNotOpen, std::to_string(i)).withCause(e);
        }
        BOOST_CHECK(e.rootCause().type() == Error::InvalidArgument);
        BOOST_CHECK(e.rootCause().message() == "0");
        BOOST_CHECK(e.message() == "31");

        std::string expected;
        for (int i = 31; i >= 0; --i) {
            if (!expected.empty()) {
                expected += ": ";
            }
            expected += std::to_string(i);
        }
        BOOST_CHECK(e.toString() == expected);
    }
}

BOOST_AUTO_TEST_SUITE_END()
