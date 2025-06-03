#include "Contribute.h"
#include "Contribute_p.h"

#include <regex>
#include <utility>
#include <mutex>

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

    ContribLocator ContribLocator::fromString(const std::string_view &token) {
        if (token.empty()) {
            return {};
        }

        // A single regex to handle all cases: id/sid, id[version]/sid, and sid
        static std::regex pattern(R"((\w+)(\[(\d+(\.\d+){0,3})\])?(\/(\w+))?)");
        std::match_results<std::string_view::const_iterator> matches;
        if (std::regex_match(token.begin(), token.end(), matches, pattern)) {
            std::string id = matches[1].str();
            std::string ver = matches[3].matched ? matches[3].str() : std::string();
            std::string sid = matches[6].matched ? matches[6].str() : std::string();
            if (!ver.empty()) {
                return {id, stdc::VersionNumber::fromString(ver), sid};
            }
            if (!sid.empty()) {
                return {id, sid};
            }
            return {id};
        }
        return {};
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

    std::string_view ContribSpec::category() const {
        __stdc_impl_t;
        return impl.category;
    }

    std::string_view ContribSpec::id() const {
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

    bool ContribCategory::loadSpec(ContribSpec *spec, ContribSpec::State state, Error *error) {
        __stdc_impl_t;

        auto spec_impl = spec->_impl.get();
        switch (state) {
            case ContribSpec::Initialized: {
                std::unique_lock<std::shared_mutex> lock(impl.su_mtx());
                auto lib = spec_impl->package;
                auto it = impl.contributes.insert(impl.contributes.end(), spec);
                impl.indexes[lib->id][lib->version][spec_impl->id] = it;
                return true;
            }

            case ContribSpec::Ready:
            case ContribSpec::Finished: {
                return true;
            }

            case ContribSpec::Deleted: {
                std::unique_lock<std::shared_mutex> lock(impl.su_mtx());
                auto lib = spec_impl->package;
                auto it = impl.indexes.find(lib->id);
                if (it == impl.indexes.end()) {
                    return true;
                }
                auto &versionMap = it->second;
                auto it2 = versionMap.find(lib->version);
                if (it2 == versionMap.end()) {
                    return true;
                }
                auto &inferenceMap = it2->second;
                auto it3 = inferenceMap.find(spec_impl->id);
                if (it3 == inferenceMap.end()) {
                    return true;
                }
                impl.contributes.erase(it3->second);
                inferenceMap.erase(it3);
                if (inferenceMap.empty()) {
                    versionMap.erase(it2);
                    if (versionMap.empty()) {
                        impl.indexes.erase(it);
                    }
                }
                return true;
            }
            default:
                break;
        }
        return false;
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