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

    // A signed and an unsigned integer are distinct types, but both answer isInt().
    {
        JsonValue i(-1);
        BOOST_CHECK(i.type() == JsonValue::Int);
        BOOST_CHECK(i.isInt());
        BOOST_CHECK(!i.isUInt());

        JsonValue u(uint32_t(1));
        BOOST_CHECK(u.type() == JsonValue::UInt);
        BOOST_CHECK(u.isInt());
        BOOST_CHECK(u.isUInt());
    }
    // isNumber() spans all three numeric types, and nothing else.
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

// \c Undefined is stored as null, so it cannot be told apart from a real null afterwards.
//
// \warning These expectations record a known defect (issue A5), not the intended contract. When the
//          state becomes representable they must be inverted rather than deleted.
BOOST_AUTO_TEST_CASE(test_JsonValue_UndefinedIsUnreachable) {
    JsonValue explicitly(JsonValue::Undefined);
    BOOST_CHECK(explicitly.type() == JsonValue::Null);
    BOOST_CHECK(!explicitly.isUndefined());

    // The value handed back for a key that is not there is equally indistinguishable, so a caller
    // cannot tell a missing field from one that is present and null.
    JsonValue obj = JsonValue::fromJson(R"({"present": null})", false);
    BOOST_CHECK(!obj["missing"].isUndefined());
    BOOST_CHECK(obj["missing"].isNull());
    BOOST_CHECK(obj["present"].isNull());
    BOOST_CHECK(obj["missing"] == obj["present"]);
}

BOOST_AUTO_TEST_CASE(test_JsonValue_Conversions) {
    // Each numeric conversion accepts all three numeric types.
    {
        BOOST_CHECK(JsonValue(42).toInt() == 42);
        BOOST_CHECK(JsonValue(uint32_t(42)).toInt() == 42);
        BOOST_CHECK(JsonValue(42.9).toInt() == 42); // truncated, not rounded
        BOOST_CHECK(JsonValue(-42).toInt() == -42);

        BOOST_CHECK(JsonValue(42).toUInt() == 42u);
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
        BOOST_CHECK(s.toUInt(7) == 7u);
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
        JsonObject objectFallback{{"k", JsonValue(1)}};
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
//
// \note This is as close as a test can get to the proxy containers standing in for nlohmann's own
//       object and array types. \c JsonValue exposes no mutating operation at all, so their
//       \c erase and \c at overloads are unreachable from here - which is exactly why the defects
//       once sitting in them (issues A6 and A7) went unnoticed for so long. Whether parsing and
//       dumping happen to exercise the iterator copy path is up to nlohmann's internals and is not
//       something this test can pin down. Treat the coverage below as covering the public contract,
//       not those containers.
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

BOOST_AUTO_TEST_SUITE_END()
