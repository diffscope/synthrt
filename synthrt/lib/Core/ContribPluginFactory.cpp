#include "ContribPluginFactory_p.h"

#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include <stdcorelib/support/json.h>

#include "ContribInterpreterPlugin.h"
#include "ContribLocator.h"

namespace srt {

    namespace {

        bool metadataProvidesInterpreter(const stdc::json::Value &metadata,
                                         const ContribTriplet &triplet) {
            if (!metadata.isObject()) {
                return false;
            }

            const auto &name = metadata["name"];
            if (!name.isString() || !ContribLocator::isValidSegment(name.toString())) {
                return false;
            }

            const auto &interpreters = metadata["interpreters"];
            if (!interpreters.isArray() || interpreters.toArray().empty()) {
                return false;
            }

            std::set<ContribTriplet> declarations;
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

                ContribTriplet declaration(declaredInterface.toString(),
                                           static_cast<int>(levelValue),
                                           declaredVariant.toString());
                if (declaration == triplet) {
                    matches = true;
                }
                if (!declarations.insert(std::move(declaration)).second) {
                    return false;
                }
            }
            return matches;
        }

    }

    stdc::plugin::PluginLoader *
        ContribPluginFactory::findInterpreter(std::string_view iid,
                                              const ContribTriplet &triplet) const {
        for (auto loader : plugins(iid)) {
            if (metadataProvidesInterpreter(loader->metadata(), triplet)) {
                return loader;
            }
        }
        return nullptr;
    }

    Expected<ContribInterpreter *>
        ContribPluginFactory::loadInterpreter(stdc::plugin::PluginLoader *loader,
                                              const ContribTriplet &triplet) {
        if (!loader) {
            return Error(Error::InvalidArgument, "interpreter plugin loader must not be null");
        }

        InterpreterKey key(loader, triplet);
        const auto cached = m_interpreters.find(key);
        if (cached != m_interpreters.end()) {
            return cached->second.get();
        }

        if (!loader->load()) {
            return Error(Error::FileNotOpen, "failed to load contribution interpreter plugin: " +
                                                 loader->errorMessage());
        }

        auto plugin = static_cast<ContribInterpreterPlugin *>(loader->plugin());
        auto result = plugin->create(triplet);
        if (!result) {
            return result.takeError().withContext("failed to create contribution interpreter");
        }
        auto interpreter = result.take();
        if (!interpreter) {
            return Error(Error::InvalidFormat,
                         "contribution interpreter plugin returned a null interpreter");
        }

        auto validators = interpreter->createImportValidators();
        if (!validators) {
            return validators.takeError().withContext(
                "contribution interpreter failed to create import validators");
        }
        auto preparedValidators = validators.take();
        for (const auto &validator : preparedValidators) {
            if (!validator) {
                return Error(Error::InvalidFormat,
                             "contribution interpreter returned a null import validator");
            }
        }

        for (auto &validator : preparedValidators) {
            m_importValidators.push_back(validator.get());
            m_importValidatorData.push_back(std::move(validator));
        }

        auto value = interpreter.get();
        m_interpreterViews.push_back(value);
        m_interpreters.emplace(std::move(key), std::move(interpreter));
        return value;
    }

    stdc::array_view<ContribInterpreter *> ContribPluginFactory::interpreters() const {
        return m_interpreterViews;
    }

    stdc::array_view<ContribImportValidator *> ContribPluginFactory::importValidators() const {
        return m_importValidators;
    }

}
