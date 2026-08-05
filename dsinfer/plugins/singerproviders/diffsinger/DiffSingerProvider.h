#ifndef DSINFER_DIFFSINGERPROVIDER_H
#define DSINFER_DIFFSINGERPROVIDER_H

#include <synthrt/SVS/SingerProvider.h>

namespace ds {

    class DiffSingerProvider : public srt::SingerProvider {
    public:
        DiffSingerProvider();
        ~DiffSingerProvider();

    public:
        int apiLevel() const override;

        srt::Expected<srt::UNO<srt::SingerConfiguration>>
            createConfiguration(const srt::SingerSpec *spec) const override;
    };

}

#endif // DSINFER_DIFFSINGERPROVIDER_H