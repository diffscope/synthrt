#pragma once

#include <synthrt/Core/Core/NamedObject.h>

#include <string>

#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Core/srt_core_global.h>
#include <synthrt/SVS/srt_svs_global.h>

namespace srt::svs {

    class SingerConfiguration;
    class SingerSpec;

    class SRT_SVS_EXPORT SingerProvider : public core::NamedObject {
    public:
        explicit SingerProvider(const SingerSpec *spec);
        ~SingerProvider() override;

    public:
        const SingerSpec *spec() const { return _spec; }

    public:
        virtual int apiLevel() const = 0;
        virtual core::Expected<core::NO<SingerConfiguration>>
            createConfiguration(const SingerSpec *spec) const = 0;

    protected:
        const SingerSpec *_spec = nullptr;
    };

}
