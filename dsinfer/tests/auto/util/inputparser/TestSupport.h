#ifndef DSINFER_TESTS_UTIL_INPUTPARSER_TESTSUPPORT_H
#define DSINFER_TESTS_UTIL_INPUTPARSER_TESTSUPPORT_H

#include <stdexcept>
#include <string>

#include <synthrt/Support/Json.h>

#include <boost/test/unit_test.hpp>

namespace {

    srt::JsonObject parseObject(const std::string &text) {
        stdc::json::ParseError error;
        auto value = srt::JsonValue::fromJson(text, true, &error);
        if (error || !value.isObject()) {
            throw std::runtime_error("failed to parse test JSON object");
        }
        return value.toObject();
    }

    srt::JsonObject completeInput() {
        return parseObject(R"({
            "duration": 1.5,
            "steps": 20,
            "depth": 0.75,
            "words": [{
                "phones": [{
                    "token": "a",
                    "language": "en",
                    "tone": 2,
                    "start": 0.25,
                    "speakers": [{"name": "main", "proportion": 0.8}]
                }],
                "notes": [{
                    "key": "C4+75",
                    "cents": 25,
                    "duration": 1.5,
                    "glide": "up",
                    "is_rest": false
                }]
            }],
            "parameters": [
                {
                    "tag": "pitch",
                    "dynamic": true,
                    "values": [60.0, 60.5],
                    "interval": 0.005,
                    "retake": {"start": 0.2, "end": 0.8}
                },
                {"tag": "energy", "value": 1.0},
                {"tag": "future-extension", "value": 2.0}
            ],
            "speakers": [{
                "name": "main",
                "dynamic": true,
                "values": [1.0, 0.9],
                "interval": 0.005
            }]
        })");
    }

}

#endif // DSINFER_TESTS_UTIL_INPUTPARSER_TESTSUPPORT_H
