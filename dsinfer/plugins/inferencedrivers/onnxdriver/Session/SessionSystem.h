#ifndef DSINFER_ONNXDRIVER_SESSIONSYSTEM_H
#define DSINFER_ONNXDRIVER_SESSIONSYSTEM_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <ios>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>

#include "SessionImage.h"

namespace ds::onnxdriver {

    /// Stores model images shared by sessions created from one driver.
    struct SessionSystem {
        struct ImageData {
            std::unique_ptr<SessionImage> image;
            size_t referenceCount = 0;
        };

        struct ImageGroup {
            std::filesystem::path path;
            std::streamsize size = 0;
            std::array<uint8_t, 32> hash{};
            std::map<bool, ImageData> images;
        };

        struct HashSizeKey {
            std::streamsize size;
            std::array<uint8_t, 32> hash{};

            bool operator<(const HashSizeKey &other) const;
        };

        std::list<ImageGroup> imageList;

        using ListIterator = decltype(imageList)::iterator;

        std::map<std::filesystem::path::string_type, ListIterator> pathMap;
        std::map<HashSizeKey, ListIterator> hashSizeMap;
        std::mutex openMutex;
        std::shared_mutex mutex;
    };

}

#endif // DSINFER_ONNXDRIVER_SESSIONSYSTEM_H
