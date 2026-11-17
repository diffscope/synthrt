#ifndef DSINFER_TESTS_UTIL_INPUTPARSER_TESTSUPPORT_H
#define DSINFER_TESTS_UTIL_INPUTPARSER_TESTSUPPORT_H

#include <stdexcept>
#include <string>
#include <utility>

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

    template <class Parser>
    void checkMalformedWordInputs(Parser parse) {
        const std::pair<const char *, const char *> inputs[] = {
            {R"({"words":null})",                                                             "words"                           },
            {R"({"words":3})",                                                                "words"                           },
            {R"({"words":{}})",                                                               "words"                           },
            {R"({"words":"bad"})",                                                            "words"                           },
            {R"({"words":[true]})",                                                           "words[]"                         },
            {R"({"words":[{"phones":{}}]})",                                                  "words[].phones"                  },
            {R"({"words":[{"phones":[false]}]})",                                             "words[].phones[]"                },
            {R"({"words":[{"notes":{}}]})",                                                   "words[].notes"                   },
            {R"({"words":[{"notes":[false]}]})",                                              "words[].notes[]"                 },
            {R"({"words":[{"phones":[{"token":1}]}]})",                                       "words[].phones[].token"          },
            {R"({"words":[{"phones":[{"language":false}]}]})",                                "words[].phones[].language"       },
            {R"({"words":[{"phones":[{"tone":1.5}]}]})",                                      "words[].phones[].tone"           },
            {R"({"words":[{"phones":[{"start":"bad"}]}]})",                                   "words[].phones[].start"          },
            {R"({"words":[{"phones":[{"speakers":{}}]}]})",                                   "words[].phones[].speakers"       },
            {R"({"words":[{"phones":[{"speakers":[false]}]}]})",                              "words[].phones[].speakers[]"     },
            {R"({"words":[{"phones":[{"speakers":[{}]}]}]})",                                 "words[].phones[].speakers[].name"},
            {R"({"words":[{"phones":[{"speakers":[{"name":true}]}]}]})",
             "words[].phones[].speakers[].name"                                                                                 },
            {R"({"words":[{"phones":[{"speakers":[{"name":"main","proportion":"bad"}]}]}]})",
             "words[].phones[].speakers[].proportion"                                                                           },
            {R"({"words":[{"notes":[{"key":false}]}]})",                                      "words[].notes[].key"             },
            {R"({"words":[{"notes":[{"key":"H4+0"}]}]})",                                     "words[].notes[].key"             },
            {R"({"words":[{"notes":[{"key":"C4+2147483648"}]}]})",                            "words[].notes[].key"             },
            {R"({"words":[{"notes":[{"key":2147483648}]}]})",                                 "words[].notes[].key"             },
            {R"({"words":[{"notes":[{"key":-2147483649}]}]})",                                "words[].notes[].key"             },
            {R"({"words":[{"notes":[{"cents":"bad"}]}]})",                                    "words[].notes[].cents"           },
            {R"({"words":[{"notes":[{"cents":2147483648}]}]})",                               "words[].notes[].cents"           },
            {R"({"words":[{"notes":[{"duration":false}]}]})",                                 "words[].notes[].duration"        },
            {R"({"words":[{"notes":[{"glide":true}]}]})",                                     "words[].notes[].glide"           },
            {R"({"words":[{"notes":[{"glide":"sideways"}]}]})",                               "words[].notes[].glide"           },
            {R"({"words":[{"notes":[{"is_rest":0}]}]})",                                      "words[].notes[].is_rest"         },
            {R"({"words":[{"notes":[{"key":2147483647,"cents":100}]}]})",                     "words[].notes[].key"             },
        };
        for (const auto &[text, field] : inputs) {
            BOOST_TEST_CONTEXT(text) {
                auto result = parse(parseObject(text));
                BOOST_REQUIRE(!result);
                BOOST_CHECK(result.error().code() == srt::Error::InvalidFormat);
                BOOST_CHECK(result.error().message().find(field) != std::string::npos);
            }
        }
    }

    template <class Parser>
    void checkMalformedCurveInputs(Parser parse) {
        const std::pair<const char *, const char *> inputs[] = {
            {R"({"parameters":{}})",                                                                  "parameters"          },
            {R"({"parameters":[false]})",                                                             "parameters[]"        },
            {R"({"parameters":[{}]})",                                                                "parameters[].tag"    },
            {R"({"parameters":[{"tag":false}]})",                                                     "parameters[].tag"    },
            {R"({"parameters":[{"tag":"pitch","dynamic":1}]})",                                       "parameters[].dynamic"},
            {R"({"parameters":[{"tag":"pitch"}]})",                                                   "parameters[].value"  },
            {R"({"parameters":[{"tag":"pitch","value":false}]})",                                     "parameters[].value"  },
            {R"({"parameters":[{"tag":"pitch","dynamic":true}]})",                                    "parameters[].values" },
            {R"({"parameters":[{"tag":"pitch","dynamic":true,"values":false}]})",
             "parameters[].values"                                                                                          },
            {R"({"parameters":[{"tag":"pitch","dynamic":true,"values":[1,"bad"],"interval":0.01}]})",
             "parameters[].values"                                                                                          },
            {R"({"parameters":[{"tag":"pitch","dynamic":true,"values":[1]}]})",
             "parameters[].interval"                                                                                        },
            {R"({"parameters":[{"tag":"pitch","dynamic":true,"values":[1],"interval":0}]})",
             "parameters[].interval"                                                                                        },
            {R"({"parameters":[{"tag":"pitch","dynamic":true,"values":[1],"interval":-1}]})",
             "parameters[].interval"                                                                                        },
            {R"({"speakers":{}})",                                                                    "speakers"            },
            {R"({"speakers":[false]})",                                                               "speakers[]"          },
            {R"({"speakers":[{}]})",                                                                  "speakers[].name"     },
            {R"({"speakers":[{"name":false}]})",                                                      "speakers[].name"     },
            {R"({"speakers":[{"name":"pitch","dynamic":1}]})",                                        "speakers[].dynamic"  },
            {R"({"speakers":[{"name":"pitch"}]})",                                                    "speakers[].value"    },
            {R"({"speakers":[{"name":"pitch","value":false}]})",                                      "speakers[].value"    },
            {R"({"speakers":[{"name":"pitch","dynamic":true}]})",                                     "speakers[].values"   },
            {R"({"speakers":[{"name":"pitch","dynamic":true,"values":false}]})",
             "speakers[].values"                                                                                            },
            {R"({"speakers":[{"name":"pitch","dynamic":true,"values":[1,"bad"],"interval":0.01}]})",
             "speakers[].values"                                                                                            },
            {R"({"speakers":[{"name":"pitch","dynamic":true,"values":[1]}]})",
             "speakers[].interval"                                                                                          },
            {R"({"speakers":[{"name":"pitch","dynamic":true,"values":[1],"interval":0}]})",
             "speakers[].interval"                                                                                          },
            {R"({"speakers":[{"name":"pitch","dynamic":true,"values":[1],"interval":-1}]})",
             "speakers[].interval"                                                                                          },
            {R"({"parameters":[{"tag":"pitch","value":60,"retake":false}]})",
             "parameters[].retake"                                                                                          },
            {R"({"parameters":[{"tag":"pitch","value":60,"retake":{}}]})",                            "parameters[].retake" },
            {R"({"parameters":[{"tag":"pitch","value":60,"retake":{"start":0}}]})",
             "parameters[].retake"                                                                                          },
            {R"({"parameters":[{"tag":"pitch","value":60,"retake":{"start":0,"end":"bad"}}]})",
             "parameters[].retake"                                                                                          },
        };
        for (const auto &[text, field] : inputs) {
            BOOST_TEST_CONTEXT(text) {
                auto result = parse(parseObject(text));
                BOOST_REQUIRE(!result);
                BOOST_CHECK(result.error().code() == srt::Error::InvalidFormat);
                BOOST_CHECK(result.error().message().find(field) != std::string::npos);
            }
        }
    }

}

#endif // DSINFER_TESTS_UTIL_INPUTPARSER_TESTSUPPORT_H
