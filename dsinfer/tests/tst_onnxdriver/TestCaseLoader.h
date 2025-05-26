// CaseLoader.h
#ifndef TST_ONNXDRIVER_CASE_LOADER_H
#define TST_ONNXDRIVER_CASE_LOADER_H

#include <stdexcept>
#include <string>
#include <optional>
#include <filesystem>
#include <dsinfer/Api/Drivers/Onnx/OnnxDriverApi.h>

namespace test {

    class TestCaseException : public std::runtime_error {
        using std::runtime_error::runtime_error;
    };

    struct TestCaseMeta {
        std::string test_id;
        std::string description;
        std::filesystem::path model_path;
    };

    struct TestCaseData {
        TestCaseData();
        TestCaseData(TestCaseMeta meta, srt::NO<ds::Api::Onnx::SessionStartInput> input,
                     srt::NO<ds::Api::Onnx::SessionResult> expectedResult);

        TestCaseMeta meta;
        srt::NO<ds::Api::Onnx::SessionStartInput> sessionInput;
        srt::NO<ds::Api::Onnx::SessionResult> expectedResult;
    };

    class TestCaseLoader {
    public:
        static TestCaseData load(const std::filesystem::path &jsonPath);
    };

} // namespace test

#endif //  TST_ONNXDRIVER_CASE_LOADER_H
