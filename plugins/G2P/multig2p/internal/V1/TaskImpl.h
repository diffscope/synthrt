#pragma once

#include "../TaskImplBase.h"

namespace srt::g2p::plugins::Multig2p::Internal::V1 {
    class Multig2pTaskImpl final : public Internal::Multig2pTaskImplBase {
    public:
        using Internal::Multig2pTaskImplBase::Multig2pTaskImplBase;

        srt::core::Expected<srt::core::NO<srt::g2p::TaskResult>>
        start(const srt::core::NO<srt::g2p::TaskInput> &input) override;
    };

}
