#ifndef SYNTHRT_PACKAGELOADER_P_H
#define SYNTHRT_PACKAGELOADER_P_H

#include <filesystem>
#include <memory>

#include <synthrt/Core/PackageHandle.h>
#include <synthrt/Core/SynthUnit.h>
#include <synthrt/Support/Expected.h>

#include "PackageHandle_p.h"

namespace srt {

    /// Coordinates Package inspection, probing, loading, and commit.
    class PackageLoader {
    public:
        explicit PackageLoader(SynthUnit &synthUnit);

        Expected<PackageHandle> open(const std::filesystem::path &path, SynthUnit::OpenMode mode);

    private:
        Expected<PackageHandle> openDataOnly(const std::filesystem::path &path);
        Expected<PackageHandle> openLoaded(const std::filesystem::path &path);
        Expected<std::shared_ptr<PackageData>> readPackage(const std::filesystem::path &path,
                                                           bool candidateOnly = false);

        SynthUnit *m_synthUnit;
    };

}

#endif // SYNTHRT_PACKAGELOADER_P_H
