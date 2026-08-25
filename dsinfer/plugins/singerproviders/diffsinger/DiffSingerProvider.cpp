#include "DiffSingerProvider.h"

#include <string_view>

#include <stdcorelib/path.h>

#include <dsinfer/Api/Singers/DiffSinger/1/DiffSingerApiL1.h>
#include <synthrt/Core/PackageHandle.h>

#include "DiffSingerPipelineExecInstance.h"

namespace ds {

    namespace Ds = Api::DiffSinger::L1;

    namespace {

        srt::Expected<void> validateKnownImports(const srt::ContribSpec &spec) {
            for (const auto &import : spec.imports()) {
                const auto &role = import.role();
                std::string_view expectedInterface;
                std::string_view expectedVariant;
                int expectedLevel;
                if (role == "duration") {
                    expectedInterface = Api::Duration::L1::API_INTERFACE;
                    expectedVariant = Api::Duration::L1::API_VARIANT;
                    expectedLevel = Api::Duration::L1::API_LEVEL;
                } else if (role == "pitch") {
                    expectedInterface = Api::Pitch::L1::API_INTERFACE;
                    expectedVariant = Api::Pitch::L1::API_VARIANT;
                    expectedLevel = Api::Pitch::L1::API_LEVEL;
                } else if (role == "variance") {
                    expectedInterface = Api::Variance::L1::API_INTERFACE;
                    expectedVariant = Api::Variance::L1::API_VARIANT;
                    expectedLevel = Api::Variance::L1::API_LEVEL;
                } else if (role == "acoustic") {
                    expectedInterface = Api::Acoustic::L1::API_INTERFACE;
                    expectedVariant = Api::Acoustic::L1::API_VARIANT;
                    expectedLevel = Api::Acoustic::L1::API_LEVEL;
                } else if (role == "vocoder") {
                    expectedInterface = Api::Vocoder::L1::API_INTERFACE;
                    expectedVariant = Api::Vocoder::L1::API_VARIANT;
                    expectedLevel = Api::Vocoder::L1::API_LEVEL;
                } else {
                    continue;
                }

                auto package = spec.package();
                auto *target = package.resolve(import.locator());
                if (!target) {
                    return srt::Error(srt::Error::FileNotFound,
                                      "DiffSinger inference import target is unavailable");
                }
                if (target->interface() != expectedInterface ||
                    target->variant() != expectedVariant || target->level() != expectedLevel) {
                    return srt::Error(
                        srt::Error::InvalidFormat,
                        "DiffSinger inference import has an incompatible contract identity");
                }
            }
            return {};
        }

    }

    static inline std::string formatErrorMessage(const std::string &msgPrefix,
                                                 const std::vector<std::string> &errorList);

    DiffSingerProvider::DiffSingerProvider() = default;

    DiffSingerProvider::~DiffSingerProvider() = default;

    srt::Expected<std::unique_ptr<srt::ContribConfiguration>>
        DiffSingerProvider::createConfiguration(const srt::ContribSpec &spec) const {
        const auto &singerSpec = *spec.as<srt::SingerSpec>();
        const auto &config = singerSpec.manifestConfiguration().toObject();
        auto result = std::make_unique<Ds::DiffSingerConfiguration>();

        // Collect all the errors and return to user
        bool hasErrors = false;
        std::vector<std::string> errorList;

        auto collectError = [&](auto &&msg) {
            hasErrors = true;
            errorList.emplace_back(std::forward<decltype(msg)>(msg));
        };

        // [REQUIRED] dict, path (json value is string)
        {
            static_assert(std::is_same_v<decltype(result->dict), std::filesystem::path>);
            if (const auto it = config.find("dict"); it != config.end()) {
                if (!it->second.isString()) {
                    collectError(R"(string field "dict" type mismatch)");
                } else {
                    result->dict =
                        stdc::path::clean_path(singerSpec.declarationPath().parent_path() /
                                               stdc::path::from_utf8(it->second.toString()));
                }
            } else {
                collectError(R"(string field "dict" is missing)");
            }
        } // dict

        if (hasErrors) {
            return srt::Error{
                srt::Error::InvalidFormat,
                formatErrorMessage("error parsing diffsinger configuration", errorList),
            };
        }
        return std::unique_ptr<srt::ContribConfiguration>(std::move(result));
    }

    srt::Expected<void> DiffSingerProvider::validateImports(const srt::ContribSpec &spec) const {
        return validateKnownImports(spec);
    }

    srt::Expected<std::unique_ptr<srt::SingerPipelineExecInstance>>
        DiffSingerProvider::createPipeline(srt::SingerSpec &spec,
                                           const srt::SingerPipelineRuntimeOptions &) {
        return std::unique_ptr<srt::SingerPipelineExecInstance>(
            new DiffSingerPipelineExecInstance(spec));
    }

    static inline std::string formatErrorMessage(const std::string &msgPrefix,
                                                 const std::vector<std::string> &errorList) {
        const std::string middlePart = " (";
        const std::string countSuffix = " errors found):\n";

        size_t totalLength = msgPrefix.size() + middlePart.size() +
                             std::to_string(errorList.size()).size() + countSuffix.size();

        for (size_t i = 0; i < errorList.size(); ++i) {
            totalLength += std::to_string(i + 1).size() + 2; // index + ". "
            totalLength += errorList[i].size();
            if (i != errorList.size() - 1) {
                totalLength += 2; // "; "
            }
        }

        std::string result;
        result.reserve(totalLength);

        result.append(msgPrefix);
        result.append(middlePart);
        result.append(std::to_string(errorList.size()));
        result.append(countSuffix);

        for (size_t i = 0; i < errorList.size(); ++i) {
            result.append(std::to_string(i + 1));
            result.append(". ");
            result.append(errorList[i]);
            if (i != errorList.size() - 1) {
                result.append(";\n");
            }
        }

        return result;
    }

}
