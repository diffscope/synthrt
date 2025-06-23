#ifndef DSINFER_INTERPRETER_COMMON_IDMAPPINGLOADER_H
#define DSINFER_INTERPRETER_COMMON_IDMAPPINGLOADER_H

#include <string>
#include <map>
#include <filesystem>

namespace ds::InterpreterCommon {
    class ErrorCollector;

    bool loadIdMapping(const std::string &fieldName, const std::filesystem::path &path,
                                std::map<std::string, int> &out, ErrorCollector *ec = nullptr);
}

#endif // DSINFER_INTERPRETER_COMMON_IDMAPPINGLOADER_H