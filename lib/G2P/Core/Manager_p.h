#pragma once

#include <synthrt/G2P/Core/Manager.h>

#include "Core/PackageManager_p.h"

namespace srt::g2p {

    class Manager::Impl : public PackageManager::Impl {
    public:
        explicit Impl(Manager *q);
        ~Impl() override;
    };

} // namespace srt::g2p
