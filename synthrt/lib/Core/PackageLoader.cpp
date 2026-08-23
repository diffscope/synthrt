#include "PackageLoader_p.h"

#include "SynthUnit_p.h"

namespace srt {

    PackageLoader::PackageLoader(SynthUnit &synthUnit) : m_synthUnit(&synthUnit) {
    }

    Expected<PackageHandle> PackageLoader::open(const std::filesystem::path &path,
                                                SynthUnit::OpenMode mode) {
        m_synthUnit->_impl->packageLoadingBegun = true;

        switch (mode) {
            case SynthUnit::DataOnly:
                return openDataOnly(path);
            case SynthUnit::Load:
                return openLoaded(path);
            default:
                return Error(Error::InvalidArgument, "unknown Package open mode");
        }
    }

    Expected<PackageHandle> PackageLoader::openDataOnly(const std::filesystem::path &) {
        return Error(Error::NotImplemented,
                     "DataOnly Package inspection is not implemented by the current Core "
                     "migration layer");
    }

    Expected<PackageHandle> PackageLoader::openLoaded(const std::filesystem::path &) {
        return Error(Error::NotImplemented,
                     "Package probing and loading are not implemented by the current Core "
                     "migration layer");
    }

}
