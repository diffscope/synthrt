#include "Contribute.h"
#include "Contribute_p.h"

#include <cassert>
#include <utility>
#include <mutex>
#include <cstdlib>

#include <stdcorelib/str.h>
#include <stdcorelib/pimpl.h>

#include "PackageRef_p.h"
#include "SynthUnit_p.h"

namespace srt {

    /// Returns whether \a token matches the grammar's \c version production, being up to four
    /// dot-separated runs of digits with no leading zero.
    ///
    /// \c stdc::VersionNumber::fromString() is not a substitute. It accepts trailing rubbish and
    /// extra segments, so a reference such as <tt>pkg=1.2.3.4.5</tt> would otherwise parse.
    static bool isValidVersion(const std::string_view &token) {
        size_t begin = 0;
        int segments = 0;
        while (true) {
            const auto dot = token.find('.', begin);
            const auto segment =
                dot == std::string_view::npos ? token.substr(begin) : token.substr(begin, dot - begin);
            if (segment.empty() || ++segments > 4) {
                return false;
            }
            if (segment.size() > 1 && segment.front() == '0') {
                return false;
            }
            for (const auto ch : segment) {
                if (ch < '0' || ch > '9') {
                    return false;
                }
            }
            if (dot == std::string_view::npos) {
                return true;
            }
            begin = dot + 1;
        }
    }

    std::string ContribLocator::toString() const {
        std::string res = _package;
        if (!_version.isEmpty()) {
            res += '=';
            res += _version.toString();
        }
        if (!_id.empty()) {
            res += ':';
            if (!_category.empty()) {
                res += _category;
                res += '/';
            }
            res += _id;
        }
        return res;
    }

    ContribLocator ContribLocator::fromString(const std::string_view &token) {
        if (token.empty()) {
            return {};
        }

        ContribLocator result;

        // The reference splits on the single ":" that separates the package from the contribute.
        // Both separators are excluded from every identifier, so each can occur at most once and
        // its position is unambiguous.
        std::string_view left = token;
        const auto colon = token.find(':');
        if (colon != std::string_view::npos) {
            left = token.substr(0, colon);
            const auto right = token.substr(colon + 1);
            if (right.empty() || right.find(':') != std::string_view::npos) {
                return {};
            }

            // Right of the colon the slash count decides the shape, since neither a category nor
            // a contribute identifier may contain one.
            const auto slash = right.find('/');
            if (slash == std::string_view::npos) {
                result._id = right;
            } else {
                const auto id = right.substr(slash + 1);
                if (id.find('/') != std::string_view::npos) {
                    return {};
                }
                result._category = right.substr(0, slash);
                result._id = id;
                if (!isValidSegment(result._category)) {
                    return {};
                }
            }
            if (!isValidSegment(result._id)) {
                return {};
            }
        }

        if (left.empty()) {
            // A bare ":" names nothing, whereas ":singer/main" names a contribute of the package
            // being resolved against.
            return colon == std::string_view::npos ? ContribLocator() : result;
        }

        const auto equals = left.find('=');
        if (equals != std::string_view::npos) {
            const auto package = left.substr(0, equals);
            if (package.empty()) {
                // A version without a package to apply it to.
                return {};
            }
            const auto version = left.substr(equals + 1);
            if (!isValidVersion(version)) {
                return {};
            }
            result._package = package;
            result._version = stdc::VersionNumber::fromString(version);
        } else {
            result._package = left;
        }
        if (!isValidPackageId(result._package)) {
            return {};
        }
        return result;
    }

    bool ContribLocator::isValidSegment(const std::string_view &token) {
        if (token.empty()) {
            return false;
        }
        for (const auto ch : token) {
            const bool ok = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                            (ch >= '0' && ch <= '9') || ch == '_' || ch == '-';
            if (!ok) {
                return false;
            }
        }
        return true;
    }

    bool ContribLocator::isValidPackageId(const std::string_view &token) {
        if (token.empty()) {
            return false;
        }
        size_t begin = 0;
        while (true) {
            const auto slash = token.find('/', begin);
            const auto segment =
                slash == std::string_view::npos ? token.substr(begin) : token.substr(begin, slash - begin);
            if (!isValidSegment(segment)) {
                return false;
            }
            if (slash == std::string_view::npos) {
                return true;
            }
            begin = slash + 1;
        }
    }

    ContribSpec::~ContribSpec() = default;

    const std::string &ContribSpec::category() const {
        stdc_impl_t;
        return impl.category;
    }

    const std::string &ContribSpec::id() const {
        stdc_impl_t;
        return impl.id;
    }

    ContribSpec::State ContribSpec::state() const {
        stdc_impl_t;
        return impl.state;
    }

    PackageRef ContribSpec::parent() const {
        stdc_impl_t;
        return PackageRef(impl.package);
    }

    SynthUnit *ContribSpec::SU() const {
        stdc_impl_t;
        return impl.package->su;
    }

    ContribSpec::ContribSpec(Impl &impl) : _impl(&impl) {
    }

    ContribSpec::ContribSpec(std::string category) : _impl(new Impl(std::move(category))) {
    }

    std::vector<ContribSpec *>
        ContribCategory::Impl::findContributes(const ContribLocator &loc) const {
        std::shared_lock<std::shared_mutex> lock(su_mtx());

        // Resolution happens within one category, so a reference naming a different one cannot
        // match here. An empty category means the caller left the kind open, which this lookup
        // answers from its own contributes.
        if (!loc.category().empty() && loc.category() != name) {
            return {};
        }

        // The package and version are filled in before a reference reaches this point, by
        // "Fix imports" for singer imports and by the caller otherwise.
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
        stdc_impl_t;
        return impl.name;
    }

    SynthUnit *ContribCategory::SU() const {
        stdc_impl_t;
        return impl.su;
    }

    Expected<void> ContribCategory::loadSpec(ContribSpec *spec, ContribSpec::State state) {
        stdc_impl_t;

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
        stdc_impl_t;
        return impl.findContributes(loc);
    }

    ContribCategory::ContribCategory(Impl &impl) : ObjectPool(impl) {
    }

    ContribCategory::ContribCategory(std::string name, SynthUnit *su)
        : ObjectPool(*new Impl(this, std::move(name), su)) {
        // The name appears in a reference between the ":" and the "/", so anything outside a
        // segment would produce references that cannot be parsed back. Categories are registered
        // by plugins, which makes this worth checking rather than assuming.
        assert(ContribLocator::isValidSegment(ContribCategory::name()) &&
               "a contribute category name must match ^[A-Za-z0-9_-]+$");
    }

}