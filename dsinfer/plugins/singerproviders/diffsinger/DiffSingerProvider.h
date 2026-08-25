#ifndef DSINFER_DIFFSINGERPROVIDER_H
#define DSINFER_DIFFSINGERPROVIDER_H

#include <memory>
#include <vector>

#include <synthrt/SVS/SingerProvider.h>

namespace ds {

    class DiffSingerProvider : public srt::SingerProvider {
    public:
        DiffSingerProvider();
        ~DiffSingerProvider();

    public:
        srt::Expected<std::vector<std::unique_ptr<srt::ContribImportValidator>>>
            createImportValidators() const override;

        srt::Expected<std::vector<std::unique_ptr<srt::ContribSpecExtension>>>
            createExtensions(srt::ContribSpec &spec) const override;

        srt::Expected<std::unique_ptr<srt::ContribConfiguration>>
            createConfiguration(const srt::ContribSpec &spec) const override;
    };

}

#endif // DSINFER_DIFFSINGERPROVIDER_H
