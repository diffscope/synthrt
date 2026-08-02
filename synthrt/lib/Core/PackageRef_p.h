#ifndef SYNTHRT_PACKAGEREF_P_H
#define SYNTHRT_PACKAGEREF_P_H

#include <string>
#include <map>
#include <filesystem>

#include <stdcorelib/adt/vlarray.h>

#include <synthrt/Support/Expected.h>
#include <synthrt/Core/PackageRef.h>

namespace srt {

    class ContribSpec;

    class ContribCategory;

    class PackageData {
    public:
        explicit PackageData(SynthUnit *su) : su(su) {
        }
        ~PackageData();

    public:
        Expected<void>
            parse(const std::filesystem::path &dir,
                  const std::map<std::string, ContribCategory *, std::less<>> &categories,
                  stdc::vlarray_base<ContribSpec *> *outContributes);

        static Expected<JsonObject> readDesc(const std::filesystem::path &dir);

        SynthUnit *su;

        std::filesystem::path path;
        std::string id;

        stdc::VersionNumber version;
        DisplayText name;
        DisplayText description;
        DisplayText vendor;
        DisplayPath readme;
        DisplayPath license;
        std::string url;

        std::map<std::string, std::map<std::string, ContribSpec *, std::less<>>, std::less<>>
            contributes; // category -> [ name -> spec ]

        stdc::vlarray<PackageDependency> dependencies;

        // state
        Error err;
        bool loaded = false;
    };

}

#endif // SYNTHRT_PACKAGEREF_P_H
