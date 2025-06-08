#include "DiffSingerProvider.h"

#include <stdcorelib/path.h>

#include <dsinfer/Api/Singers/DiffSinger/1/DiffSingerApiL1.h>

namespace ds {

    namespace Ds = Api::DiffSinger::L1;

    static inline std::string formatErrorMessage(const std::string &msgPrefix,
                                             const std::vector<std::string> &errorList);

    DiffSingerProvider::DiffSingerProvider() = default;

    DiffSingerProvider::~DiffSingerProvider() = default;

    int DiffSingerProvider::apiLevel() const {
        return Ds::API_LEVEL;
    }

    srt::Expected<srt::NO<srt::SingerConfiguration>>
        DiffSingerProvider::createConfiguration(const srt::SingerSpec *spec) const {
        if (!spec) {
            // fatal error: null pointer, return immediately
            return srt::Error{
                srt::Error::InvalidArgument,
                "fatal in createConfiguration: SingerSpec is nullptr",
            };
        }

        const auto &config = spec->manifestConfiguration();
        auto result = srt::NO<Ds::DiffSingerConfiguration>::create();

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
                    result->dict = stdc::path::clean_path(
                        spec->path() / stdc::path::from_utf8(it->second.toStringView()));
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
        return result;
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