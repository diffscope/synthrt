#ifndef SYNTHRT_PACKAGEREF_P_H
#define SYNTHRT_PACKAGEREF_P_H

#include <string>
#include <map>
#include <filesystem>

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
        bool parse(const std::filesystem::path &dir,
                   const std::map<std::string, ContribCategory *, std::less<>> &categories,
                   std::vector<ContribSpec *> *outContributes, Error *error);

        static bool readDesc(const std::filesystem::path &dir, JsonObject *out, Error *error);

        SynthUnit *su;

        std::filesystem::path path;
        std::string id;

        stdc::VersionNumber version;
        stdc::VersionNumber compatVersion;

        DisplayText description;
        DisplayText vendor;
        DisplayText copyright;
        std::filesystem::path readme;
        std::string url;

        std::map<std::string, std::map<std::string, ContribSpec *, std::less<>>, std::less<>>
            contributes; // category -> [ name -> spec ]

        std::vector<PackageDependency> dependencies;

        // state
        Error err;
        bool loaded = false;
    };

}

#endif // SYNTHRT_PACKAGEREF_P_H