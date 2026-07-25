#pragma once

#include <string>

#include <synthrt/Core/Core/NamedObject.h>
#include <synthrt/Core/Support/Expected.h>

#include <synthrt/Driver/srt_driver_global.h>

namespace srt::driver {

    class InferenceSession;

    class SRT_DRIVER_EXPORT InferenceDriverInitArgs : public srt::core::NamedObject {
    public:
        inline InferenceDriverInitArgs(std::string name, int version)
            : srt::core::NamedObject(std::move(name)), version(version) {
        }

        int version;
    };

    /// InferenceDriver - DiffSinger inference driver interface.
    ///
    /// \note An instance of \c InferenceDriver needs to be added to the inference
    /// category with the ID "dsdriver" before it can be called by the inference
    /// interpreters.
    class SRT_DRIVER_EXPORT InferenceDriver : public srt::core::NamedObject {
    public:
        virtual ~InferenceDriver() = default;

        /// Related singer arch.
        virtual std::string arch() const = 0;

        /// Driver backend identifier.
        virtual std::string backend() const = 0;

        virtual srt::core::Expected<void> initialize(
            const srt::core::NO<InferenceDriverInitArgs> &args) = 0;

        virtual srt::core::NO<InferenceSession> createSession() = 0;
    };

}
