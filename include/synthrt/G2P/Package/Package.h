#ifndef SRT_G2P_PACKAGE_PACKAGE_H
#define SRT_G2P_PACKAGE_PACKAGE_H

#include <filesystem>
#include <string>
#include <vector>

#include <stdcorelib/support/versionnumber.h>

#include <synthrt/Core/Module/Module.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/G2P/Support/DisplayText.h>
#include <synthrt/G2P/Support/Error.h>
#include <synthrt/G2P/srt_g2p_global.h>

namespace srt::g2p {

    class Manager;
    class PackageManager;
    class PackageData;
    class ScopedPackageRef;

    /// ModuleSpec alias — ModuleSpec is defined in srt::core; alias it here
    /// for use in G2P-facing signatures. srt::g2p does not define its own
    /// ModuleSpec subclass (the srt::core version is sufficient).
    using ModuleSpec = srt::core::ModuleSpec;

    /// Package - handle to a loaded G2P package.
    ///
    /// Migrated from LangCore::Package. Wraps a PackageData pointer managed by
    /// PackageManager. A default-constructed Package refers to a static empty
    /// PackageData (isValid() == false when mgr is null).
    class SRT_G2P_EXPORT Package {
    public:
        Package();
        ~Package();

        bool isValid() const { return Mgr() != nullptr; }
        bool close();

        const std::string &id() const;
        stdc::VersionNumber version() const;
        stdc::VersionNumber compatVersion() const;

        DisplayText description() const;
        DisplayText vendor() const;
        DisplayText copyright() const;
        const std::filesystem::path &readme() const;
        const std::string &url() const;

        std::vector<ModuleSpec *> moduleSpecs(const std::string_view &category) const;
        ModuleSpec *moduleSpec(const std::string_view &category, const std::string_view &id) const;

        const std::filesystem::path &path() const;
        Error error() const;
        bool isLoaded() const;
        PackageManager *Mgr() const;

    private:
        explicit Package(PackageData *data) : _data(data) {}
        PackageData *_data;

        friend class PackageManager;
        friend class ScopedPackageRef;
    };

    /// ScopedPackageRef - RAII wrapper that force-closes the package on destruction.
    class SRT_G2P_EXPORT ScopedPackageRef : public Package {
    public:
        ScopedPackageRef() = default;
        explicit ScopedPackageRef(Package &&RHS) { std::swap(_data, RHS._data); }
        ~ScopedPackageRef() { forceClose(); }

        ScopedPackageRef &operator=(Package &&RHS) {
            if (this != &RHS) {
                forceClose();
                std::swap(_data, RHS._data);
            }
            return *this;
        }

        Package release() {
            Package ref;
            std::swap(_data, ref._data);
            return ref;
        }

    private:
        void forceClose();
        STDCORELIB_DISABLE_COPY(ScopedPackageRef);
    };

} // namespace srt::g2p

#endif // SRT_G2P_PACKAGE_PACKAGE_H
