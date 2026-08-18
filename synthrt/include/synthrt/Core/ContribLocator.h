#ifndef SYNTHRT_CONTRIBLOCATOR_H
#define SYNTHRT_CONTRIBLOCATOR_H

#include <string>
#include <string_view>

#include <stdcorelib/support/versionnumber.h>

#include <synthrt/synthrt_global.h>

namespace srt {

    /// Reference to a package, or to one contribute inside a package.
    ///
    /// Syntax:
    /// \code
    ///     reference     = package-part [ ":" contrib-part ]
    ///                   / ":" contrib-part
    ///     package-part  = package-id [ "=" version ]
    ///     contrib-part  = [ category "/" ] contrib-id
    ///
    ///     package-id    = segment *( "/" segment )
    ///     category      = segment
    ///     contrib-id    = segment
    ///     segment       = 1*( ALPHA / DIGIT / "_" / "-" )
    /// \endcode
    ///
    /// \code
    ///     vendor/sample=1.0.0.0:inference/acoustic    // fully qualified
    ///     vendor/sample=1.0.0.0:singer/main
    ///     vendor/sample=1.0.0.0:acoustic              // category left to resolution
    ///     vendor/sample:singer/main                   // version left to resolution
    ///     :singer/main                                // the current package
    ///     vendor/sample=1.0.0.0                       // the package itself
    /// \endcode
    ///
    /// The leading \c ":" is not optional when no package part is given: \c singer/main on its own
    /// is a reference to the package of that name, not to the contribute \c main of category
    /// \c singer.
    ///
    /// The category carries the contribute kind as data rather than as punctuation, so a new kind
    /// of contribute needs no change to this grammar. It may be omitted, in which case resolution
    /// searches every category and reports an ambiguity if more than one matches - which can
    /// happen, as a singer and an inference of the same name may coexist in one package.
    class SYNTHRT_EXPORT ContribLocator {
    public:
        inline ContribLocator() = default;

        inline ContribLocator(std::string package, stdc::VersionNumber version,
                              std::string category, std::string id)
            : _package(std::move(package)), _version(std::move(version)),
              _category(std::move(category)), _id(std::move(id)) {
        }

        inline ContribLocator(std::string package, stdc::VersionNumber version)
            : _package(std::move(package)), _version(std::move(version)) {
        }

        /// Returns the package identifier, empty when the reference names no package.
        inline const std::string &package() const {
            return _package;
        }

        /// Returns the package version, empty when the reference pins none.
        inline stdc::VersionNumber version() const {
            return _version;
        }

        /// Returns the contribute category, empty when the reference leaves it to resolution.
        inline const std::string &category() const {
            return _category;
        }

        /// Returns the contribute identifier, empty when the reference names a package only.
        inline const std::string &id() const {
            return _id;
        }

        /// Returns whether this reference names nothing at all.
        inline bool isEmpty() const {
            return _package.empty() && _id.empty();
        }

        /// Renders the reference, which parses back to an equal value.
        std::string toString() const;

        /// Parses a reference, returning an empty locator when \a token does not match the
        /// grammar.
        static ContribLocator fromString(const std::string_view &token);

        /// Returns whether \a token is a \c segment, the shape every category and contribute
        /// identifier takes.
        static bool isValidSegment(const std::string_view &token);

        /// Returns whether \a token is a \c package-id, one or more segments joined by \c "/".
        static bool isValidPackageId(const std::string_view &token);

        inline bool operator==(const ContribLocator &other) const {
            return _package == other._package && _version == other._version &&
                   _category == other._category && _id == other._id;
        }

        inline bool operator!=(const ContribLocator &other) const {
            return !(*this == other);
        }

    protected:
        std::string _package;
        stdc::VersionNumber _version;
        std::string _category;
        std::string _id;
    };

}

#endif // SYNTHRT_CONTRIBLOCATOR_H
