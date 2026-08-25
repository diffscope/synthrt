#ifndef TEST_ONNXDRIVER_CASE_LOADER_H
#define TEST_ONNXDRIVER_CASE_LOADER_H

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

#include <dsinfer/Api/Drivers/Onnx/OnnxDriverApi.h>

namespace test {

    class TestCaseException : public std::runtime_error {
        using std::runtime_error::runtime_error;
    };

    struct TestCaseMeta {
        std::string testId;
        std::string description;
        std::filesystem::path modelPath;
    };

    struct TestCaseData {
        TestCaseData();
        TestCaseData(TestCaseMeta meta,
                     std::shared_ptr<ds::Api::Onnx::SessionStartInput> sessionInput,
                     std::shared_ptr<ds::Api::Onnx::SessionResult> expectedResult);

        TestCaseMeta meta;
        std::shared_ptr<ds::Api::Onnx::SessionStartInput> sessionInput;
        std::shared_ptr<ds::Api::Onnx::SessionResult> expectedResult;
    };

    class TestCaseLoader {
    public:
        static std::shared_ptr<TestCaseData> load(const std::filesystem::path &jsonPath);
    };

}

#endif // TEST_ONNXDRIVER_CASE_LOADER_H
