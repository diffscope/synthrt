#include "DiffSingerProvider.h"

#include <stdcorelib/path.h>

#include <diffsinger/Infer/dsinfer/Api/Singers/DiffSinger/1/DiffSingerApiL1.h>

namespace srt::svs {

    namespace Ds = svs::Api::DiffSinger::L1;

    static inline std::string formatErrorMessage(const std::string &msgPrefix,
                                             const std::vector<std::string> &errorList);

    DiffSingerProvider::DiffSingerProvider(const SingerSpec *spec)
        : SingerProvider(spec) {
    }

    DiffSingerProvider::~DiffSingerProvider() = default;

    int DiffSingerProvider::apiLevel() const {
        return Ds::API_LEVEL;
    }

    core::Expected<core::NO<SingerConfiguration>>
        DiffSingerProvider::createConfiguration(const SingerSpec *spec) const {
        if (!spec) {
            // fatal error: null pointer, return immediately
            return core::Error{
                core::Error::InvalidArgument,
                "fatal in createConfiguration: SingerSpec is nullptr",
            };
        }

        const auto &config = spec->manifestConfiguration();
        auto result = core::NO<Ds::DiffSingerConfiguration>::create();

        // Collect all the errors and return to user
        bool hasErrors = false;
        std::vector<std::string> errorList;

        auto collectError = [&](auto &&msg) {
            hasErrors = true;
            errorList.emplace_back(std::forward<decltype(msg)>(msg));
        };

        // Legacy packages may provide a top-level dict. Standard packages resolve
        // language resources through package metadata and G2P, so dict is optional here.
        {
            static_assert(std::is_same_v<decltype(result->dict), std::filesystem::path>);
            if (const auto it = config.find("dict"); it != config.end()) {
                if (!it->second.isString()) {
                    collectError(R"(string field "dict" type mismatch)");
                } else {
                    result->dict = stdc::path::clean_path(
                        spec->path() / stdc::path::from_utf8(it->second.toStringView()));
                }
            }
        } // dict

        if (hasErrors) {
            return core::Error{
                core::Error::InvalidFormat,
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
