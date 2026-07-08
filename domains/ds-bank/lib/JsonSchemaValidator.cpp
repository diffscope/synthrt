#include <stdcorelib/str.h>

#include <diffsinger/Bank/JsonSchemaValidator.h>

using srt::core::JsonArray;
using srt::core::JsonObject;
using srt::core::JsonValue;

namespace ds::bank {

    static bool checkType(const JsonValue &value, const std::string &type) {
        if (type == "string") {
            return value.isString();
        }
        if (type == "number") {
            return value.isNumber();
        }
        if (type == "boolean") {
            return value.isBool();
        }
        if (type == "array") {
            return value.isArray();
        }
        if (type == "object") {
            return value.isObject();
        }
        // Unknown type constraint: accept anything.
        return true;
    }

    bool JsonSchemaValidator::validate(const JsonValue &value, const JsonObject &schema,
                                       std::vector<std::string> &errors) const {
        // type
        {
            auto it = schema.find("type");
            if (it != schema.end()) {
                if (it->second.isString()) {
                    const auto &type = it->second.toString();
                    if (!checkType(value, type)) {
                        errors.emplace_back(
                            stdc::formatN("type mismatch: expected %1", type));
                        return false;
                    }
                } else if (it->second.isArray()) {
                    bool ok = false;
                    for (const auto &t : it->second.toArray()) {
                        if (t.isString() && checkType(value, t.toString())) {
                            ok = true;
                            break;
                        }
                    }
                    if (!ok) {
                        errors.emplace_back("type mismatch: value did not match any allowed type");
                        return false;
                    }
                }
            }
        }

        // object: required + properties
        if (value.isObject()) {
            const auto obj = value.toObject();

            // required
            {
                auto it = schema.find("required");
                if (it != schema.end() && it->second.isArray()) {
                    for (const auto &req : it->second.toArray()) {
                        if (!req.isString()) {
                            continue;
                        }
                        const auto &name = req.toString();
                        if (obj.find(name) == obj.end()) {
                            errors.emplace_back(
                                stdc::formatN("missing required property: %1", name));
                        }
                    }
                }
            }

            // properties
            {
                auto it = schema.find("properties");
                if (it != schema.end() && it->second.isObject()) {
                    const auto &props = it->second.toObject();
                    for (const auto &prop : props) {
                        auto mit = obj.find(prop.first);
                        if (mit == obj.end() || !prop.second.isObject()) {
                            continue;
                        }
                        std::vector<std::string> childErrors;
                        if (!validate(mit->second, prop.second.toObject(), childErrors)) {
                            for (auto &e : childErrors) {
                                errors.emplace_back(
                                    stdc::formatN("%1: %2", prop.first, e));
                            }
                        }
                    }
                }
            }
        }

        // array: items
        if (value.isArray()) {
            auto it = schema.find("items");
            if (it != schema.end() && it->second.isObject()) {
                const auto &itemSchema = it->second.toObject();
                size_t i = 0;
                for (const auto &elem : value.toArray()) {
                    std::vector<std::string> childErrors;
                    if (!validate(elem, itemSchema, childErrors)) {
                        for (auto &e : childErrors) {
                            errors.emplace_back(stdc::formatN("[%1]: %2", i, e));
                        }
                    }
                    ++i;
                }
            }
        }

        return errors.empty();
    }

}
