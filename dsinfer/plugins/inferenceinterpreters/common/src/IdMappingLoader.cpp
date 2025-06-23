#include <InterpreterCommon/IdMappingLoader.h>

#include <fstream>

#include <stdcorelib/str.h>
#include <stdcorelib/path.h>

#include <synthrt/Support/JSON.h>

#include <InterpreterCommon/ErrorCollector.h>

namespace ds::InterpreterCommon {
    bool loadIdMapping(const std::string &fieldName, const std::filesystem::path &path,
                       std::map<std::string, int> &out, ErrorCollector *ec) {
        std::ifstream file(path);
        if (!file.is_open()) {
            if (ec) {
                ec->collectError(stdc::formatN(R"(error loading "%1": %2 file not found)",
                                               fieldName, stdc::path::to_utf8(path)));
            }
            return false;
        }
        file.seekg(0, std::ios::end);
        auto size = file.tellg();
        std::string buffer(size, '\0');
        file.seekg(0);
        file.read(buffer.data(), size);

        std::string errString;
        auto j = srt::JsonValue::fromJson(buffer, true, &errString);
        if (!errString.empty()) {
            if (ec) {
                ec->collectError(std::move(errString));
            }
            return false;
        }

        if (!j.isObject()) {
            if (ec) {
                ec->collectError(
                    stdc::formatN(R"(error loading "%1": outer JSON is not an object)", fieldName));
            }
            return false;
        }

        const auto &obj = j.toObject();
        bool flag = true;
        for (const auto &[key, value] : obj) {
            if (!value.isInt()) {
                flag = false;
                if (ec) {
                    ec->collectError(stdc::formatN(
                        R"(error loading "%1": value of key "%2" is not int)", fieldName, key));
                }
            } else {
                out[key] = static_cast<int>(value.toInt());
            }
        }
        return flag;
    }
}