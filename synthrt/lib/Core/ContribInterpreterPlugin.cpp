#include "ContribInterpreterPlugin.h"

#include <limits>
#include <set>
#include <utility>

#include "ContribLocator.h"

namespace srt {

    Expected<std::vector<ContribTriplet>>
        ContribInterpreterPlugin::readInterpreterDeclarations(const stdc::json::Value &metadata) {
        if (!metadata.isObject()) {
            return Error(Error::InvalidFormat, "plugin metadata is not an object");
        }

        const auto &name = metadata["name"];
        if (!name.isString() || !ContribLocator::isValidSegment(name.toString())) {
            return Error(Error::InvalidFormat, "plugin metadata has no valid name");
        }

        const auto &interpreters = metadata["interpreters"];
        if (!interpreters.isArray() || interpreters.toArray().empty()) {
            return Error(Error::InvalidFormat,
                         "plugin metadata declares no interpreters array, or an empty one");
        }

        std::vector<ContribTriplet> declarations;
        std::set<ContribTriplet> seen;
        for (const auto &value : interpreters.toArray()) {
            const auto position = "interpreter " + std::to_string(declarations.size());
            if (!value.isObject()) {
                return Error(Error::InvalidFormat, position + " is not an object");
            }

            const auto &declaredInterface = value["interface"];
            if (!declaredInterface.isString() ||
                !ContribLocator::isValidDottedId(declaredInterface.toString())) {
                return Error(Error::InvalidFormat,
                             position + " has no interface that is a valid dotted identifier");
            }
            const auto &declaredVariant = value["variant"];
            if (!declaredVariant.isString() ||
                !ContribLocator::isValidDottedId(declaredVariant.toString())) {
                return Error(Error::InvalidFormat,
                             position + " has no variant that is a valid dotted identifier");
            }
            const auto &declaredLevel = value["level"];
            if (!declaredLevel.isInt()) {
                return Error(Error::InvalidFormat, position + " has no integer level");
            }
            const auto level = declaredLevel.toInt();
            if (level <= 0 || level > std::numeric_limits<int>::max()) {
                return Error(Error::InvalidFormat,
                             position + " has a level outside 1 to " +
                                 std::to_string(std::numeric_limits<int>::max()));
            }

            ContribTriplet triplet(declaredInterface.toString(), static_cast<int>(level),
                                   declaredVariant.toString());
            if (!seen.insert(triplet).second) {
                return Error(Error::InvalidFormat, position + " repeats an earlier declaration");
            }
            declarations.push_back(std::move(triplet));
        }
        return declarations;
    }

}
