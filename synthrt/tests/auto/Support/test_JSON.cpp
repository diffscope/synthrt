#include <synthrt/Support/JSON.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

using srt::JsonArray;
using srt::JsonObject;
using srt::JsonValue;

BOOST_AUTO_TEST_SUITE(test_JSON)

BOOST_AUTO_TEST_CASE(test_JsonValue_Types) {
    BOOST_CHECK(JsonValue().type() == JsonValue::Null);
    BOOST_CHECK(JsonValue().isNull());

    BOOST_CHECK(JsonValue(true).type() == JsonValue::Bool);
    BOOST_CHECK(JsonValue(1.5).type() == JsonValue::Double);
    BOOST_CHECK(JsonValue(std::string("s")).type() == JsonValue::String);
    BOOST_CHECK(JsonValue("s").type() == JsonValue::String);
    BOOST_CHECK(JsonValue(JsonArray{}).type() == JsonValue::Array);
    BOOST_CHECK(JsonValue(JsonObject{}).type() == JsonValue::Object);

    // An integer is one type whichever way it was written, signed or not.
    {
        JsonValue i(-1);
        BOOST_CHECK(i.type() == JsonValue::Int);
        BOOST_CHECK(i.isInt());

        JsonValue u(uint32_t(1));
        BOOST_CHECK(u.type() == JsonValue::Int);
        BOOST_CHECK(u.isInt());

        // Except above the range int64_t has, where there is nothing left to be exact with.
        JsonValue big(UINT64_MAX);
        BOOST_CHECK(big.type() == JsonValue::Double);
    }
    // isNumber() spans both numeric types, and nothing else.
    {
        BOOST_CHECK(JsonValue(1).isNumber());
        BOOST_CHECK(JsonValue(uint32_t(1)).isNumber());
        BOOST_CHECK(JsonValue(1.5).isNumber());
        BOOST_CHECK(!JsonValue("1").isNumber());
        BOOST_CHECK(!JsonValue(true).isNumber());
        BOOST_CHECK(!JsonValue().isNumber());
    }
    // A counted string may contain an embedded null.
    {
        JsonValue v("ab\0cd", 5);
        BOOST_CHECK(v.toString().size() == 5);
        BOOST_CHECK(v.toString() == std::string("ab\0cd", 5));
    }
    // Binary is its own type and is not a string.
    {
        const uint8_t raw[] = {0x00, 0x01, 0xFE, 0xFF};
        JsonValue v(raw, 4);
        BOOST_CHECK(v.type() == JsonValue::Binary);
        BOOST_CHECK(!v.isString());
        BOOST_CHECK(v.toBinary().size() == 4);
        BOOST_CHECK(v.toBinaryView().size() == 4);
        BOOST_CHECK(v.toBinary()[3] == 0xFF);
    }
}

// A key that is not there reads as null, the same as a key that is there and null.
//
// \note There is no separate undefined state. Telling the two apart is what the object itself is
//       for, since \c toObject hands back the map.
BOOST_AUTO_TEST_CASE(test_JsonValue_MissingKeyIsNull) {
    JsonValue obj = JsonValue::fromJson(R"({"present": null})", false);
    BOOST_CHECK(obj["missing"].isNull());
    BOOST_CHECK(obj["present"].isNull());
    BOOST_CHECK(obj["missing"] == obj["present"]);

    const JsonObject &map = obj.toObject();
    BOOST_CHECK(map.find("present") != map.end());
    BOOST_CHECK(map.find("missing") == map.end());
}

BOOST_AUTO_TEST_CASE(test_JsonValue_Conversions) {
    // Each numeric conversion accepts both numeric types.
    {
        BOOST_CHECK(JsonValue(42).toInt() == 42);
        BOOST_CHECK(JsonValue(uint32_t(42)).toInt() == 42);
        BOOST_CHECK(JsonValue(42.9).toInt() == 42); // truncated, not rounded
        BOOST_CHECK(JsonValue(-42).toInt() == -42);

        BOOST_CHECK(JsonValue(1.5).toDouble() == 1.5);
        BOOST_CHECK(JsonValue(3).toDouble() == 3.0);
    }
    // A mismatched type yields the default rather than throwing.
    {
        JsonValue s("text");
        BOOST_CHECK(s.toBool() == false);
        BOOST_CHECK(s.toBool(true) == true);
        BOOST_CHECK(s.toInt() == 0);
        BOOST_CHECK(s.toInt(7) == 7);
        BOOST_CHECK(s.toDouble(1.5) == 1.5);
        BOOST_CHECK(s.toArray().empty());
        BOOST_CHECK(s.toObject().empty());
        BOOST_CHECK(s.toBinary().empty());

        JsonValue n;
        BOOST_CHECK(n.toString() == "");
        BOOST_CHECK(n.toString("fallback") == "fallback");
        BOOST_CHECK(n.toStringView("fallback") == "fallback");
    }
    // A bool is not a number and a number is not a bool.
    {
        BOOST_CHECK(JsonValue(true).toInt(-1) == -1);
        BOOST_CHECK(JsonValue(1).toBool(false) == false);
    }
    // The defaulted overloads hand back the caller's own object.
    {
        JsonArray arrayFallback{JsonValue(1)};
        JsonObject objectFallback{
            {"k", JsonValue(1)}
        };
        JsonValue s("text");
        BOOST_CHECK(s.toArray(arrayFallback).size() == 1);
        BOOST_CHECK(s.toObject(objectFallback).size() == 1);
    }
}

BOOST_AUTO_TEST_CASE(test_JsonValue_Subscript) {
    JsonValue v = JsonValue::fromJson(R"({"a": 1, "nested": {"b": [10, 20]}})", false);
    BOOST_REQUIRE(v.isObject());

    BOOST_CHECK(v["a"].toInt() == 1);
    BOOST_CHECK(v["nested"]["b"][0].toInt() == 10);
    BOOST_CHECK(v["nested"]["b"][1].toInt() == 20);

    // Every way of missing the target yields a null value instead of throwing, so chains like the
    // one above never need a check at each step.
    BOOST_CHECK(v["absent"].isNull());
    BOOST_CHECK(v["nested"]["b"][2].isNull());   // index past the end
    BOOST_CHECK(v["a"]["b"].isNull());           // subscripting a number by key
    BOOST_CHECK(v["a"][size_t(0)].isNull());     // subscripting a number by index
    BOOST_CHECK(v["absent"]["deeper"].isNull()); // chaining off a missing key
}

BOOST_AUTO_TEST_CASE(test_JsonValue_Equality) {
    BOOST_CHECK(JsonValue(1) == JsonValue(1));
    BOOST_CHECK(JsonValue(1) != JsonValue(2));
    BOOST_CHECK(JsonValue("a") != JsonValue(1));
    BOOST_CHECK(JsonValue() == JsonValue());
    BOOST_CHECK(JsonValue(1) != JsonValue("1"));

    // Containers compare by content; an object ignores insertion order because it is a std::map.
    BOOST_CHECK(JsonValue::fromJson(R"({"a":1,"b":2})", false) ==
                JsonValue::fromJson(R"({"b":2,"a":1})", false));
    BOOST_CHECK(JsonValue::fromJson("[1,2]", false) != JsonValue::fromJson("[2,1]", false));
}

BOOST_AUTO_TEST_CASE(test_JsonValue_Parse) {
    // A parse failure leaves a null value behind and fills in the message.
    {
        std::string error;
        JsonValue v = JsonValue::fromJson("{not json", false, &error);
        BOOST_CHECK(v.isNull());
        BOOST_CHECK(!error.empty());
    }
    // The error output is optional.
    {
        BOOST_CHECK(JsonValue::fromJson("{not json", false).isNull());
    }
    // A successful parse leaves the message untouched.
    {
        std::string error;
        JsonValue v = JsonValue::fromJson(R"({"a": 1})", false, &error);
        BOOST_CHECK(v.isObject());
        BOOST_CHECK(error.empty());
    }
    // Comments are a parse error unless asked for.
    {
        const std::string_view withComments = R"({"a": 1 /* note */, "b": 2 })";
        BOOST_CHECK(JsonValue::fromJson(withComments, false).isNull());

        JsonValue v = JsonValue::fromJson(withComments, true);
        BOOST_REQUIRE(v.isObject());
        BOOST_CHECK(v["a"].toInt() == 1);
        BOOST_CHECK(v["b"].toInt() == 2);
    }
}

BOOST_AUTO_TEST_CASE(test_JsonValue_Serialize) {
    // Keys come out sorted, since the underlying container is ordered.
    {
        JsonValue v = JsonValue::fromJson(R"({"b": 2, "a": 1})", false);
        BOOST_CHECK(v.toJson() == R"({"a":1,"b":2})");
    }
    // A positive indent switches to the multi-line form.
    {
        JsonValue v = JsonValue::fromJson(R"({"a":1})", false);
        BOOST_CHECK(v.toJson(2) == "{\n  \"a\": 1\n}");
    }
    {
        BOOST_CHECK(JsonValue().toJson() == "null");
        BOOST_CHECK(JsonValue(true).toJson() == "true");
        BOOST_CHECK(JsonValue("s").toJson() == R"("s")");
        BOOST_CHECK(JsonValue(JsonArray{}).toJson() == "[]");
        BOOST_CHECK(JsonValue(JsonObject{}).toJson() == "{}");
    }
}

// Text in, structure out, text back.
BOOST_AUTO_TEST_CASE(test_JsonValue_RoundTrip) {
    const std::string_view text = R"({
        "string": "value",
        "escaped": "quote \" backslash \\ newline \n unicode é",
        "int": -7,
        "uint": 7,
        "double": 1.25,
        "true": true,
        "false": false,
        "null": null,
        "emptyArray": [],
        "emptyObject": {},
        "array": [1, "two", 3.0, null, true, [4, 5], {"deep": "deeper"}],
        "object": {"nested": {"deeper": {"deepest": [1, 2, 3]}}}
    })";

    JsonValue parsed = JsonValue::fromJson(text, false);
    BOOST_REQUIRE(parsed.isObject());

    // Serializing and parsing again must land on an equal value.
    JsonValue reparsed = JsonValue::fromJson(parsed.toJson(), false);
    BOOST_CHECK(reparsed == parsed);
    BOOST_CHECK(reparsed.toJson() == parsed.toJson());

    BOOST_CHECK(parsed["escaped"].toString().find('\n') != std::string::npos);
    BOOST_CHECK(parsed["array"].toArray().size() == 7);
    BOOST_CHECK(parsed["array"][5][1].toInt() == 5);
    BOOST_CHECK(parsed["array"][6]["deep"].toString() == "deeper");
    BOOST_CHECK(parsed["object"]["nested"]["deeper"]["deepest"][2].toInt() == 3);
    BOOST_CHECK(parsed["emptyArray"].isArray());
    BOOST_CHECK(parsed["emptyObject"].isObject());
    BOOST_CHECK(parsed["null"].isNull());
    BOOST_CHECK(parsed["true"].toBool() == true);
    BOOST_CHECK(parsed["false"].toBool(true) == false);

    // Walking the exposed containers must agree with subscripting.
    const JsonObject &obj = parsed.toObject();
    BOOST_CHECK(obj.size() == 12);
    BOOST_CHECK(obj.find("string") != obj.end());
    BOOST_CHECK(obj.find("absent") == obj.end());
    for (const auto &item : obj) {
        BOOST_CHECK(item.second == parsed[item.first]);
    }

    const JsonArray &arr = parsed["array"].toArray();
    for (size_t i = 0; i < arr.size(); ++i) {
        BOOST_CHECK(arr[i] == parsed["array"][i]);
    }
}

BOOST_AUTO_TEST_CASE(test_JsonValue_Cbor) {
    JsonValue v = JsonValue::fromJson(R"({"a": [1, 2.5, "three"], "b": {"c": true}})", false);
    BOOST_REQUIRE(v.isObject());

    BOOST_CHECK(JsonValue::fromCbor(v.toCbor()) == v);

    // Binary survives CBOR, which is the reason for having it at all - it has no JSON text form.
    {
        const uint8_t raw[] = {0xDE, 0xAD, 0xBE, 0xEF};
        JsonValue bin(raw, 4);
        JsonValue back = JsonValue::fromCbor(bin.toCbor());
        BOOST_REQUIRE(back.type() == JsonValue::Binary);
        BOOST_CHECK(back.toBinary() == bin.toBinary());
    }
    // Malformed input reports rather than throws.
    {
        const uint8_t garbage[] = {0xFF, 0xFF, 0xFF};
        std::string error;
        JsonValue back = JsonValue::fromCbor(stdc::array_view<uint8_t>(garbage, 3), &error);
        BOOST_CHECK(back.isNull());
        BOOST_CHECK(!error.empty());
    }
}

BOOST_AUTO_TEST_CASE(test_JsonValue_ValueSemantics) {
    JsonValue original = JsonValue::fromJson(R"({"a": [1, 2], "b": "text"})", false);

    // Copying is deep enough that the two are independent, which for an immutable value shows as
    // equality that survives the source going away.
    {
        JsonValue copy(original);
        BOOST_CHECK(copy == original);

        JsonValue assigned;
        assigned = original;
        BOOST_CHECK(assigned == original);
    }
    // Moving carries the contents across.
    {
        JsonValue source = original;
        JsonValue moved(std::move(source));
        BOOST_CHECK(moved == original);

        JsonValue source2 = original;
        JsonValue moveAssigned;
        moveAssigned = std::move(source2);
        BOOST_CHECK(moveAssigned == original);
    }
    // Swap exchanges the two.
    {
        JsonValue a("first");
        JsonValue b(2);
        a.swap(b);
        BOOST_CHECK(a.toInt() == 2);
        BOOST_CHECK(b.toString() == "first");
    }
    // Self assignment must not corrupt the value.
    {
        JsonValue v = original;
        const JsonValue &alias = v;
        v = alias;
        BOOST_CHECK(v == original);
    }
    // A container built by hand behaves the same as a parsed one.
    {
        JsonObject obj;
        obj["a"] = JsonArray{JsonValue(1), JsonValue(2)};
        obj["b"] = JsonValue("text");
        BOOST_CHECK(JsonValue(std::move(obj)) == original);
    }
}

// Escapes, which are where a reader is most likely to be wrong.
BOOST_AUTO_TEST_CASE(test_JsonValue_Escapes) {
    auto parse = [](std::string_view text) {
        return JsonValue::fromJson(text, false)["k"].toString();
    };

    BOOST_CHECK(parse(R"({"k":"A"})") == "A");
    BOOST_CHECK(parse(R"({"k":"\/"})") == "/");
    BOOST_CHECK(parse(R"({"k":"\b\f\n\r\t"})") == "\b\f\n\r\t");

    // A code point outside the basic plane arrives as a surrogate pair and has to be put back
    // together before it is encoded.
    BOOST_CHECK(parse(R"({"k":"😀"})") == "\xF0\x9F\x98\x80");

    // Two bytes for U+00E9, three for U+4E2D.
    BOOST_CHECK(parse(R"({"k":"é"})") == "\xC3\xA9");
    BOOST_CHECK(parse(R"({"k":"中"})") == "\xE4\xB8\xAD");

    // A round trip through text has to preserve all of it.
    JsonValue v = JsonValue::fromJson(R"({"k":"😀  \" \\ é"})", false);
    BOOST_CHECK(JsonValue::fromJson(v.toJson(), false) == v);

    // A control character with no short form of its own comes out as a \u escape, printable text
    // does not. The backslash is spelled char(92) so that nothing here depends on how escapes nest.
    {
        const std::string bs(1, char(92));
        BOOST_CHECK(JsonValue(std::string(1, char(7))).toJson() == "\"" + bs + "u0007\"");
        BOOST_CHECK(JsonValue(std::string(1, char(10))).toJson() == "\"" + bs + "n\"");
    }
    BOOST_CHECK(JsonValue(std::string("é")).toJson() == "\"é\"");
}

// Input a reader has to turn away rather than accept and misread.
BOOST_AUTO_TEST_CASE(test_JsonValue_Rejects) {
    auto rejected = [](std::string_view text) {
        std::string error;
        JsonValue v = JsonValue::fromJson(text, false, &error);
        return v.isNull() && !error.empty();
    };

    BOOST_CHECK(rejected(""));
    BOOST_CHECK(rejected("   "));
    BOOST_CHECK(rejected("nul"));
    BOOST_CHECK(rejected("{\"a\":1} trailing"));
    BOOST_CHECK(rejected("[1,2"));
    BOOST_CHECK(rejected("[1,]"));
    BOOST_CHECK(rejected("{\"a\"}"));
    BOOST_CHECK(rejected("{a:1}"));
    BOOST_CHECK(rejected("\"unterminated"));
    BOOST_CHECK(rejected("\"a\tb\""));  // a raw tab inside a string
    BOOST_CHECK(rejected(R"("\q")"));   // an escape that means nothing
    BOOST_CHECK(rejected(R"("\u12")")); // too few hexadecimal digits

    // Written the long way round: a universal character name is still looked at inside a raw
    // string literal, and neither of these is a character.
    BOOST_CHECK(rejected("\"\\ud83d\"")); // a high surrogate with nothing after it
    BOOST_CHECK(rejected("\"\\ude00\"")); // a low surrogate on its own
    BOOST_CHECK(rejected("01"));
    BOOST_CHECK(rejected("1."));
    BOOST_CHECK(rejected(".1"));
    BOOST_CHECK(rejected("1e"));
    BOOST_CHECK(rejected("+1"));
    BOOST_CHECK(rejected("\"\xFF\xFE\"")); // not valid UTF-8

    // Nesting deep enough to run out of stack is refused rather than attempted.
    BOOST_CHECK(rejected(std::string(5000, '[')));

    // Depth within the limit still parses.
    {
        const int depth = 100;
        std::string text = std::string(size_t(depth), '[') + std::string(size_t(depth), ']');
        BOOST_CHECK(JsonValue::fromJson(text, false).isArray());
    }
}

BOOST_AUTO_TEST_CASE(test_JsonValue_Comments) {
    const std::string_view text = R"({
        // a line comment
        "a": 1, /* and a block one */
        "b": 2 // at the end
    })";

    BOOST_CHECK(JsonValue::fromJson(text, false).isNull());

    JsonValue v = JsonValue::fromJson(text, true);
    BOOST_REQUIRE(v.isObject());
    BOOST_CHECK(v["a"].toInt() == 1);
    BOOST_CHECK(v["b"].toInt() == 2);

    // A comment that never closes is an error, not a value.
    BOOST_CHECK(JsonValue::fromJson("/* unterminated", true).isNull());
}

BOOST_AUTO_TEST_CASE(test_JsonValue_Numbers) {
    // The written form decides the type, which is what a round trip has to preserve.
    {
        JsonValue v = JsonValue::fromJson(R"([1, -1, 1.0, 1e2, -0.0])", false);
        const JsonArray &a = v.toArray();
        BOOST_REQUIRE(a.size() == 5);
        BOOST_CHECK(a[0].type() == JsonValue::Int);
        BOOST_CHECK(a[1].type() == JsonValue::Int);
        BOOST_CHECK(a[2].type() == JsonValue::Double);
        BOOST_CHECK(a[3].type() == JsonValue::Double);
        BOOST_CHECK(a[4].type() == JsonValue::Double);

        // An integral double keeps its point, or it comes back as an integer.
        BOOST_CHECK(v.toJson() == "[1,-1,1.0,100.0,-0.0]");
    }
    // Comparison ignores the distinction the types keep.
    {
        BOOST_CHECK(JsonValue(int64_t(1)) == JsonValue(uint64_t(1)));
        BOOST_CHECK(JsonValue(int64_t(1)) == JsonValue(1.0));
        BOOST_CHECK(JsonValue(int64_t(-1)) != JsonValue(uint64_t(1)));
    }
    // The ends of the integer range survive a round trip exactly, which is the whole reason for
    // keeping integers apart from doubles.
    {
        for (int64_t i : {INT64_MIN, INT64_MAX, int64_t(9007199254740993)}) {
            JsonValue v(i);
            JsonValue back = JsonValue::fromJson(v.toJson(), false);
            BOOST_CHECK(back.type() == JsonValue::Int);
            BOOST_CHECK(back.toInt() == i);
        }
    }
    // Past that range there is nothing left to be exact with, so it becomes a double rather than a
    // parse error.
    {
        BOOST_CHECK(JsonValue::fromJson("9223372036854775808", false).type() == JsonValue::Double);
        BOOST_CHECK(JsonValue::fromJson("123456789012345678901234567890", false).type() ==
                    JsonValue::Double);
    }
    // Doubles have to come back bit for bit.
    {
        const double values[] = {0.1, 1.0 / 3.0, 1e-300, 1e300, 3.141592653589793};
        for (double d : values) {
            JsonValue v(d);
            BOOST_CHECK(JsonValue::fromJson(v.toJson(), false).toDouble() == d);
        }
    }
}

BOOST_AUTO_TEST_CASE(test_JsonValue_Indent) {
    JsonValue v = JsonValue::fromJson(R"({"a":[1,2],"b":{"c":null}})", false);

    BOOST_CHECK(v.toJson(2) == "{\n"
                               "  \"a\": [\n"
                               "    1,\n"
                               "    2\n"
                               "  ],\n"
                               "  \"b\": {\n"
                               "    \"c\": null\n"
                               "  }\n"
                               "}");

    // An empty container stays on one line whatever the indent.
    BOOST_CHECK(JsonValue::fromJson(R"({"a":[],"b":{}})", false).toJson(2) ==
                "{\n  \"a\": [],\n  \"b\": {}\n}");
}

BOOST_AUTO_TEST_CASE(test_JsonValue_CborShapes) {
    auto roundTrip = [](const JsonValue &v) { return JsonValue::fromCbor(v.toCbor()) == v; };

    BOOST_CHECK(roundTrip(JsonValue()));
    BOOST_CHECK(roundTrip(JsonValue(true)));
    BOOST_CHECK(roundTrip(JsonValue(false)));
    BOOST_CHECK(roundTrip(JsonValue(JsonArray{})));
    BOOST_CHECK(roundTrip(JsonValue(JsonObject{})));
    BOOST_CHECK(roundTrip(JsonValue(std::string())));
    BOOST_CHECK(roundTrip(JsonValue(std::string("中文 é"))));

    // Every width the integer encoding has, on both sides of zero.
    {
        const int64_t signedValues[] = {-1, -24, -25, -256, -65536, -4294967296LL, INT64_MIN};
        for (int64_t i : signedValues) {
            BOOST_CHECK(roundTrip(JsonValue(i)));
        }
        const uint64_t unsignedValues[] = {0, 23, 24, 255, 256, 65535, 65536, INT64_MAX};
        for (uint64_t u : unsignedValues) {
            BOOST_CHECK(roundTrip(JsonValue(u)));
        }
    }
    BOOST_CHECK(roundTrip(JsonValue(0.1)));

    // A truncated encoding reports rather than reads past the end.
    {
        JsonValue v = JsonValue::fromJson(R"({"a":[1,2,3]})", false);
        auto encoded = v.toCbor();
        encoded.resize(encoded.size() - 1);
        std::string error;
        BOOST_CHECK(JsonValue::fromCbor(encoded, &error).isNull());
        BOOST_CHECK(!error.empty());
    }
    // So does something that is not CBOR at all.
    {
        const uint8_t indefinite[] = {0x9F, 0x01, 0xFF};
        std::string error;
        BOOST_CHECK(JsonValue::fromCbor(stdc::array_view<uint8_t>(indefinite, 3), &error).isNull());
        BOOST_CHECK(!error.empty());
    }
}

// A string is a counted sequence of bytes, not a C string, so a null inside it is just a byte.
BOOST_AUTO_TEST_CASE(test_JsonValue_EmbeddedNull) {
    JsonValue parsed = JsonValue::fromJson("\"a\\u0000b\"", false);
    BOOST_REQUIRE(parsed.isString());
    BOOST_CHECK(parsed.toString().size() == 3);
    BOOST_CHECK(parsed.toString() == std::string("a\0b", 3));

    // And it has to survive going back out and coming in again.
    JsonValue back = JsonValue::fromJson(parsed.toJson(), false);
    BOOST_CHECK(back == parsed);
    BOOST_CHECK(back.toString().size() == 3);
}

// What is escaped on the way out, and what is not.
BOOST_AUTO_TEST_CASE(test_JsonValue_EscapesOnOutput) {
    const std::string bs(1, char(92));

    // Delete is a control character to a terminal but not to JSON, so it goes out as it stands.
    BOOST_CHECK(JsonValue(std::string(1, char(0x7F))).toJson() == "\"\x7F\"");

    // Every other C0 character without a mnemonic takes the numeric form.
    BOOST_CHECK(JsonValue(std::string(1, char(0x1F))).toJson() == "\"" + bs + "u001f\"");

    // Keys go through the same escaping as values, which is easy to write only for values.
    {
        JsonObject obj;
        obj[std::string("a\nb")] = JsonValue(1);
        BOOST_CHECK(JsonValue(std::move(obj)).toJson() == "{\"a" + bs + "nb\":1}");
    }
}

// Text that is not valid UTF-8 still has to come out as something that reads back.
//
// \note This is the one place the implementation makes a choice rather than following the format:
//       serializing cannot report an error, so a bad sequence becomes U+FFFD instead. Without it
//       toJson would be the only accessor that can fail.
BOOST_AUTO_TEST_CASE(test_JsonValue_InvalidUtf8OnOutput) {
    // One of each way a sequence can be wrong: a stray trailing byte, a lead byte with nothing
    // after it, an overlong encoding of a character that has a shorter one, and a surrogate.
    const std::string cases[] = {
        std::string("lone ") + char(0x81) + " trailing",
        std::string("missing ") + char(0xD0) + " trailing",
        std::string("overlong ") + char(0xC0) + char(0x80),
        std::string("surrogate ") + char(0xED) + char(0xA0) + char(0x80),
        std::string("too long ") + char(0xF9) + char(0x80) + char(0x80) + char(0x80) + char(0x80),
    };

    for (const auto &bad : cases) {
        JsonValue v(bad);
        std::string text = v.toJson();

        std::string error;
        JsonValue back = JsonValue::fromJson(text, false, &error);
        BOOST_CHECK_MESSAGE(error.empty(), error);
        BOOST_CHECK(back.isString());

        // The replacement character is there in place of what could not be written.
        BOOST_CHECK(back.toString().find("\xEF\xBF\xBD") != std::string::npos);
    }

    // Valid text is left exactly alone, replacement characters included.
    for (const char *good : {"plain ASCII", "with é and 中", "\xEF\xBF\xBD already"}) {
        JsonValue v{std::string(good)};
        BOOST_CHECK(JsonValue::fromJson(v.toJson(), false).toString() == good);
    }
}

// A value owns its string rather than pointing into the caller's.
BOOST_AUTO_TEST_CASE(test_JsonValue_StringOwnership) {
    char raw[] = "Hello";
    JsonValue fromPointer(static_cast<const char *>(raw));
    raw[1] = 'a';
    BOOST_CHECK(fromPointer.toString() == "Hello");

    std::string owned = "Hello";
    JsonValue fromString(owned);
    owned[1] = 'a';
    BOOST_CHECK(fromString.toString() == "Hello");
}

// The rest of the ways a document can be wrong, beyond those already covered.
BOOST_AUTO_TEST_CASE(test_JsonValue_MoreRejects) {
    auto rejected = [](const std::string &text) {
        std::string error;
        JsonValue v = JsonValue::fromJson(text, false, &error);
        return v.isNull() && !error.empty();
    };

    BOOST_CHECK(rejected("["));
    BOOST_CHECK(rejected("{"));
    BOOST_CHECK(rejected("[][]"));
    BOOST_CHECK(rejected("fuzzy"));
    BOOST_CHECK(rejected("[2?]"));
    BOOST_CHECK(rejected("[&%!]"));
    BOOST_CHECK(rejected(R"({"a",2})"));
    BOOST_CHECK(rejected(R"({"a":2 "b":3})"));
    BOOST_CHECK(rejected("1e1.0"));
    // Built by hand rather than as a raw string, which MSVC mishandles inside a macro argument
    // once it contains an escaped quote.
    BOOST_CHECK(rejected(std::string("\"abc") + char(92) + "\"def"));

    // An overlong encoding is the classic way to smuggle a character past a check that is looking
    // for its shorter form.
    BOOST_CHECK(rejected("\"" + std::string(1, char(0xC0)) + std::string(1, char(0x80)) + "\""));
    BOOST_CHECK(rejected("\"" + std::string(1, char(0xED)) + std::string(1, char(0xA0)) +
                         std::string(1, char(0x80)) + "\""));

    // The message says where, which is the only thing making a large document fixable.
    {
        std::string error;
        JsonValue::fromJson("{\n  \"valid\": 1,\n  invalid: 2\n}", false, &error);
        BOOST_CHECK(error.find("line 3") != std::string::npos);
        BOOST_CHECK(error.find("column 3") != std::string::npos);
    }
}

BOOST_AUTO_TEST_SUITE_END()
