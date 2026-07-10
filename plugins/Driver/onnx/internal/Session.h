#ifndef SRT_DRIVER_ONNX_SESSION_H
#define SRT_DRIVER_ONNX_SESSION_H

#include <map>
#include <memory>
#include <filesystem>
#include <functional>

#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Driver/onnx/OnnxDriverApi.h>
#include <synthrt/Core/Task/ITask.h>


namespace srt::driver::onnx {

    class Session {
    public:
        enum SessionHint {
            SH_NoHint = 0,
            SH_PreferCPUHint = 0x1,

            // Per-session EP override is encoded in bits 8-15, and the device
            // index (stored as deviceIndex+1 so that -1 maps to 0) in bits
            // 16-23. Only used when useCpu is false AND a per-session EP is
            // specified; SH_NoHint (follow global) and SH_PreferCPUHint (force
            // CPU) keep their existing semantics and bit layout untouched, so
            // callers that don't set \c ep behave exactly as before.
            //
            // SH_EPOverrideHint is a sentinel flag (bit 1) set whenever a
            // per-session EP is explicitly provided. It distinguishes an
            // explicit \c ep=CPU + deviceIndex=-1 override (which would
            // otherwise encode to 0 == SH_NoHint) from "follow global",
            // preventing the wrong SessionImage from being returned by the
            // cache.
            SH_EPOverrideHint = 0x2,
            SH_EPOffset = 8,
            SH_EPMask = 0xFF00,
            SH_DeviceOffset = 16,
            SH_DeviceMask = 0xFF0000,
        };

        Session();
        ~Session();

        Session(const Session &) = delete;
        Session &operator=(const Session &) = delete;

        Session(Session &&other) noexcept;
        Session &operator=(Session &&other) noexcept;

    public:
        srt::core::Expected<void> open(const std::filesystem::path &path,
                                       const srt::core::NO<SessionOpenArgs> &args);
        srt::core::Expected<void> close();

        const std::vector<std::string> &inputNames() const;
        const std::vector<std::string> &outputNames() const;

        srt::core::Expected<srt::core::NO<srt::core::TaskResult>>
            run(const srt::core::NO<srt::core::TaskStartInput> &input);
        srt::core::Expected<void>
            runAsync(const srt::core::NO<srt::core::TaskStartInput> &input,
                     const srt::core::ITask::StartAsyncCallback &callback);

        void terminate();

        const std::filesystem::path &path() const;
        bool isOpen() const;

        srt::core::NO<srt::core::TaskResult> result() const;

    protected:
        class Impl;
        std::unique_ptr<Impl> _impl;
    };

}

#endif // SRT_DRIVER_ONNX_SESSION_H
