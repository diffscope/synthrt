#ifndef DSINFER_DIFFSINGERPROVIDER_H
#define DSINFER_DIFFSINGERPROVIDER_H

#include <synthrt/SVS/SingerProvider.h>

namespace srt::svs {

    class DiffSingerProvider : public SingerProvider {
    public:
        explicit DiffSingerProvider(const SingerSpec *spec);
        ~DiffSingerProvider();

    public:
        int apiLevel() const override;

        core::Expected<core::NO<SingerConfiguration>>
            createConfiguration(const SingerSpec *spec) const override;
    };

}

#endif // DSINFER_DIFFSINGERPROVIDER_H