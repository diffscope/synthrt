#include <catch2/catch_test_macros.hpp>
#include <synthrt/G2P/Task/G2pTask.h>
#include <string>

TEST_CASE("G2pInputV1 carries languageId field", "[g2p][langref]") {
    srt::g2p::G2pInputV1 input;
    REQUIRE(input.g2pInput.empty());
    REQUIRE(input.languageId.empty());

    input.g2pInput.push_back("hello");
    input.languageId = "deu/default";

    REQUIRE(input.g2pInput.size() == 1);
    REQUIRE(input.g2pInput[0] == "hello");
    REQUIRE(input.languageId == "deu/default");
}

TEST_CASE("G2pInputV1 languageId is optional (empty by default)", "[g2p][langref]") {
    srt::g2p::G2pInputV1 input;
    input.g2pInput.push_back("world");

    REQUIRE(input.g2pInput[0] == "world");
    REQUIRE(input.languageId.empty());
}

TEST_CASE("G2pInputV1 supports multiple words with languageId", "[g2p][langref]") {
    srt::g2p::G2pInputV1 input;
    input.g2pInput.push_back("guten");
    input.g2pInput.push_back("tag");
    input.languageId = "deu/default";

    REQUIRE(input.g2pInput.size() == 2);
    REQUIRE(input.languageId == "deu/default");
}

TEST_CASE("G2pInputV1 supports kor/default langRef", "[g2p][langref]") {
    srt::g2p::G2pInputV1 input;
    input.g2pInput.push_back("annyeong");
    input.languageId = "kor/default";
    REQUIRE(input.languageId == "kor/default");
}

TEST_CASE("G2pInputV1 supports rus/default langRef", "[g2p][langref]") {
    srt::g2p::G2pInputV1 input;
    input.g2pInput.push_back("privet");
    input.languageId = "rus/default";
    REQUIRE(input.languageId == "rus/default");
}
