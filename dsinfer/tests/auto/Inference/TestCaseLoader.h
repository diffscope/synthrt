#ifndef TEST_INFERENCE_TESTCASELOADER_H
#define TEST_INFERENCE_TESTCASELOADER_H

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

#include <dsinfer/Api/Drivers/Onnx/OnnxDriverApi.h>

namespace test {

    /// Reports malformed or unreadable inference test case data.
    class TestCaseError : public std::runtime_error {
    public:
        using std::runtime_error::runtime_error;
    };

    /// Contains one model invocation and its expected outputs.
    struct TestCaseData {
        std::string id;
        std::string description;
        std::filesystem::path modelPath;
        std::shared_ptr<ds::Api::Onnx::SessionStartInput> input;
        std::shared_ptr<ds::Api::Onnx::SessionResult> expectedResult;
    };

    /// Loads ONNX session test cases from JSON resources.
    class TestCaseLoader {
    public:
        static TestCaseData load(const std::filesystem::path &path);
    };

}

#endif // TEST_INFERENCE_TESTCASELOADER_H
