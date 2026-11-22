#ifndef DSINFER_SINGERPROVIDERS_DIFFSINGER_DIFFSINGERPROVIDER_H
#define DSINFER_SINGERPROVIDERS_DIFFSINGER_DIFFSINGERPROVIDER_H

#include <memory>
#include <vector>

#include <synthrt/SVS/SingerProvider.h>

namespace ds {

    class DiffSingerProvider : public srt::SingerProvider {
    public:
        DiffSingerProvider();
        ~DiffSingerProvider();

    public:
        /// Interprets the singer exports of \a spec as \c Api::DiffSinger::L1::Exports.
        srt::Expected<std::unique_ptr<srt::ContribExports>>
            createExports(const srt::ContribSpec &spec) const override;

        srt::Expected<std::vector<std::unique_ptr<srt::ContribImportValidator>>>
            createImportValidators() const override;

        srt::Expected<std::vector<std::unique_ptr<srt::ContribSpecExtension>>>
            createExtensions(srt::ContribSpec &spec) const override;

        srt::Expected<std::unique_ptr<srt::ContribConfiguration>>
            createConfiguration(const srt::ContribSpec &spec) const override;
    };

}

#endif // DSINFER_SINGERPROVIDERS_DIFFSINGER_DIFFSINGERPROVIDER_H
