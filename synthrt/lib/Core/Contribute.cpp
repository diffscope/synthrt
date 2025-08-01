#include "Contribute.h"
#include "Contribute_p.h"

#include <regex>
#include <utility>
#include <mutex>
#include <cstdlib>

#include <stdcorelib/str.h>
#include <stdcorelib/pimpl.h>

#include "PackageRef_p.h"
#include "SynthUnit_p.h"

namespace srt {

    std::string ContribLocator::toString() const {
        if (_package.empty()) {
            return _id;
        }
        if (_version.isEmpty()) {
            if (_id.empty()) {
                return _package;
            }
            return stdc::formatN("%1/%2", _package, _id);
        }
        if (_id.empty()) {
            return stdc::formatN("%1[%2]", _package, _version.toString());
        }
        return stdc::formatN("%1[%2]/%3", _package, _version.toString(), _id);
    }

    // Format: id/sid, id[version]/sid, and sid
    ContribLocator ContribLocator::fromString(const std::string_view &token) {
        if (token.empty()) {
            return {};
        }

        ContribLocator result;
        size_t slashPos = token.find('/');
        if (slashPos != std::string::npos) {
            // Case: id/sid or id[version]/sid
            auto leftPart = token.substr(0, slashPos);
            auto rightPart = token.substr(slashPos + 1);
            if (!isValidLocator(rightPart)) {
                return {};
            }
            result._id = rightPart;

            size_t openBracket = leftPart.find('[');
            if (openBracket != std::string::npos) {
                if (leftPart.back() != ']') {
                    return {};
                }
                auto package = leftPart.substr(0, openBracket);
                if (!isValidLocator(package)) {
                    return {};
                }
                // id[version]
                result._package = package;
                result._version = stdc::VersionNumber::fromString(
                    leftPart.substr(openBracket + 1, leftPart.size() - openBracket - 1));
            } else {
                if (!isValidLocator(leftPart)) {
                    return {};
                }
                // just id
                result._package = leftPart;
            }
        } else {
            // Case: sid only
            if (!isValidLocator(token)) {
                return {};
            }
            result._id = token;
        }
        return result;
    }

    bool ContribLocator::isValidLocator(const std::string_view &token) {
        if (token.empty()) {
            return false;
        }
        for (const auto &ch : token) {
            switch (ch) {
                case '/':
                case '\\':
                case '[':
                case ']':
                case ':':
                case ';':
                case '\'':
                case '\"':
                    return false;
                default:
                    break;
            }
        }
        return true;
    }

    ContribSpec::~ContribSpec() = default;

    const std::string &ContribSpec::category() const {
        __stdc_impl_t;
        return impl.category;
    }

    const std::string &ContribSpec::id() const {
        __stdc_impl_t;
        return impl.id;
    }

    ContribSpec::State ContribSpec::state() const {
        __stdc_impl_t;
        return impl.state;
    }

    PackageRef ContribSpec::parent() const {
        __stdc_impl_t;
        return PackageRef(impl.package);
    }

    SynthUnit *ContribSpec::SU() const {
        __stdc_impl_t;
        return impl.package->su;
    }

    ContribSpec::ContribSpec(Impl &impl) : _impl(&impl) {
    }

    ContribSpec::ContribSpec(std::string category) : _impl(new Impl(std::move(category))) {
    }

    std::vector<ContribSpec *>
        ContribCategory::Impl::findContributes(const ContribLocator &loc) const {
        std::shared_lock<std::shared_mutex> lock(su_mtx());
        if (loc.package().empty() || loc.version().isEmpty()) {
            return {};
        }
        auto it = indexes.find(loc.package());
        if (it == indexes.end()) {
            return {};
        }
        const auto &versionMap = it->second;

        auto it2 = versionMap.find(loc.version());
        if (it2 == versionMap.end()) {
            return {};
        }
        const auto &inferenceMap = it2->second;

        if (!loc.id().empty()) {
            auto it3 = inferenceMap.find(loc.id());
            if (it3 == inferenceMap.end()) {
                return {};
            }
            return {*it3->second};
        }

        std::vector<ContribSpec *> res;
        res.reserve(inferenceMap.size());
        for (const auto &pair : inferenceMap) {
            res.push_back(*pair.second);
        }
        return res;
    }

    ContribCategory::~ContribCategory() = default;

    const std::string &ContribCategory::name() const {
        __stdc_impl_t;
        return impl.name;
    }

    SynthUnit *ContribCategory::SU() const {
        __stdc_impl_t;
        return impl.su;
    }

    Expected<void> ContribCategory::loadSpec(ContribSpec *spec, ContribSpec::State state) {
        __stdc_impl_t;

        auto spec_impl = spec->_impl.get();
        switch (state) {
            case ContribSpec::Initialized: {
                std::unique_lock<std::shared_mutex> lock(impl.su_mtx());
                auto lib = spec_impl->package;
                auto it = impl.contributes.insert(impl.contributes.end(), spec);
                impl.indexes[lib->id][lib->version][spec_impl->id] = it;
                return Expected<void>();
            }

            case ContribSpec::Ready:
            case ContribSpec::Finished: {
                return Expected<void>();
            }

            case ContribSpec::Deleted: {
                std::unique_lock<std::shared_mutex> lock(impl.su_mtx());
                auto lib = spec_impl->package;
                auto it = impl.indexes.find(lib->id);
                if (it == impl.indexes.end()) {
                    return Expected<void>();
                }
                auto &versionMap = it->second;
                auto it2 = versionMap.find(lib->version);
                if (it2 == versionMap.end()) {
                    return Expected<void>();
                }
                auto &inferenceMap = it2->second;
                auto it3 = inferenceMap.find(spec_impl->id);
                if (it3 == inferenceMap.end()) {
                    return Expected<void>();
                }
                impl.contributes.erase(it3->second);
                inferenceMap.erase(it3);
                if (inferenceMap.empty()) {
                    versionMap.erase(it2);
                    if (versionMap.empty()) {
                        impl.indexes.erase(it);
                    }
                }
                return Expected<void>();
            }
            default:
                break;
        }
        std::abort();
        return Expected<void>();
    }

    std::vector<ContribSpec *> ContribCategory::find(const ContribLocator &loc) const {
        __stdc_impl_t;
        return impl.findContributes(loc);
    }

    ContribCategory::ContribCategory(Impl &impl) : ObjectPool(impl) {
    }

    ContribCategory::ContribCategory(std::string name, SynthUnit *su)
        : ObjectPool(*new Impl(this, std::move(name), su)) {
    }

}