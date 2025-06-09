#ifndef SYNTHRT_SINGERPROVIDER_H
#define SYNTHRT_SINGERPROVIDER_H

#include <synthrt/Support/Expected.h>
#include <synthrt/SVS/SingerContrib.h>

namespace srt {

    class SingerProvider : public NamedObject {
    public:
        /// The highest singer API version currently supported by this model.
        virtual int apiLevel() const = 0;

        /// Called when \a SingerSpec loads.
        virtual Expected<NO<SingerConfiguration>>
            createConfiguration(const SingerSpec *spec) const = 0;
    };

}

#endif // SYNTHRT_SINGERPROVIDER_H