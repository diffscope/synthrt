#include "SessionSystem.h"

#include <algorithm>

namespace ds::onnxdriver {

    bool SessionSystem::HashSizeKey::operator<(const HashSizeKey &other) const {
        if (size != other.size) {
            return size < other.size;
        }
        return std::lexicographical_compare(hash.begin(), hash.end(), other.hash.begin(),
                                            other.hash.end());
    }

}
