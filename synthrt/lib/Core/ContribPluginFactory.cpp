#include "ContribPluginFactory_p.h"

#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include <stdcorelib/support/json.h>

#include "ContribInterpreterPlugin.h"
#include "ContribLocator.h"

namespace srt {

    namespace {

        bool manifestProvidesInterpreter(const stdc::json::Value &manifest,
                                         std::string_view interfaceName, std::string_view variant,
                                         int level) {
            if (!manifest.isObject()) {
                return false;
            }

            const auto &name = manifest["name"];
            if (!name.isString() || !ContribLocator::isValidSegment(name.toString())) {
                return false;
            }

            const auto &metadata = manifest["metadata"];
            if (!metadata.isObject()) {
                return false;
            }
            const auto &interpreters = metadata["interpreters"];
            if (!interpreters.isArray() || interpreters.toArray().empty()) {
                return false;
            }

            std::set<std::tuple<std::string, std::string, int64_t>> declarations;
            bool matches = false;
            for (const auto &value : interpreters.toArray()) {
                if (!value.isObject()) {
                    return false;
                }

                const auto &declaredInterface = value["interface"];
                const auto &declaredVariant = value["variant"];
                const auto &declaredLevel = value["level"];
                if (!declaredInterface.isString() || !declaredVariant.isString() ||
                    !declaredLevel.isInt() ||
                    !ContribLocator::isValidDottedId(declaredInterface.toString()) ||
                    !ContribLocator::isValidDottedId(declaredVariant.toString())) {
                    return false;
                }

                const auto levelValue = declaredLevel.toInt();
                if (levelValue <= 0 || levelValue > std::numeric_limits<int>::max()) {
                    return false;
                }

                const auto declaration = std::make_tuple(declaredInterface.toString(),
                                                         declaredVariant.toString(), levelValue);
                if (!declarations.insert(declaration).second) {
                    return false;
                }
                if (declaredInterface.toString() == interfaceName &&
                    declaredVariant.toString() == variant && levelValue == level) {
                    matches = true;
                }
            }
            return matches;
        }

    }

    stdc::plugin::PluginLoader *
        ContribPluginFactory::findInterpreter(std::string_view iid, std::string_view interfaceName,
                                              std::string_view variant, int level) const {
        for (auto *loader : plugins(iid)) {
            if (manifestProvidesInterpreter(loader->manifest(), interfaceName, variant, level)) {
                return loader;
            }
        }
        return nullptr;
    }

    Expected<ContribInterpreter *>
        ContribPluginFactory::loadInterpreter(stdc::plugin::PluginLoader *loader) {
        if (!loader) {
            return Error(Error::InvalidArgument, "interpreter plugin loader must not be null");
        }

        const auto cached = m_interpreters.find(loader);
        if (cached != m_interpreters.end()) {
            return cached->second.get();
        }

        if (!loader->load()) {
            return Error(Error::FileNotOpen, "failed to load contribution interpreter plugin: " +
                                                 loader->errorMessage());
        }

        auto *plugin = static_cast<ContribInterpreterPlugin *>(loader->plugin());
        auto result = plugin->create();
        if (!result) {
            return result.takeError().withContext("failed to create contribution interpreter");
        }
        auto interpreter = result.take();
        if (!interpreter) {
            return Error(Error::InvalidFormat,
                         "contribution interpreter plugin returned a null interpreter");
        }

        auto *value = interpreter.get();
        m_interpreters.emplace(loader, std::move(interpreter));
        return value;
    }

}
