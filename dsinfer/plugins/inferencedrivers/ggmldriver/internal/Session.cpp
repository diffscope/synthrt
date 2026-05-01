#include "Session.h"

#include <stdcorelib/pimpl.h>

#include "GgmlDriver_Logger.h"

namespace fs = std::filesystem;

namespace ds::ggmldriver {

    using ggmldriver::Log;

    class Session::Impl {
    public:
        bool opened = false;
        fs::path modelPath;
        srt::NO<Api::Common::SessionResult> sessionResult;

        Impl() : sessionResult(srt::NO<Api::Common::SessionResult>::create()) {
        }
    };

    Session::Session() : _impl(std::make_unique<Impl>()) {
    }

    Session::~Session() {
        close();
    }

    srt::Expected<void> Session::open(const fs::path &path,
                                      const srt::NO<Api::Common::SessionOpenArgs> &args) {
        __stdc_impl_t;

        if (isOpen()) {
            return srt::Error(srt::Error::SessionError, "session is already open");
        }

        if (!fs::is_regular_file(path)) {
            return srt::Error(srt::Error::FileNotOpen, "not a regular file");
        }

        fs::path canonical_path = fs::canonical(path);
        Log.srtInfo("Session - Opening GGUF model: %1", canonical_path.string());

        // TODO: Load GGUF model via ggml (Task 8)

        impl.modelPath = canonical_path;
        impl.opened = true;

        Log.srtInfo("Session - Model loaded successfully");
        return srt::Expected<void>();
    }

    srt::Expected<void> Session::close() {
        __stdc_impl_t;

        if (!impl.opened) {
            return srt::Error(srt::Error::SessionError, "session is not open");
        }

        // TODO: Release ggml resources (Task 8)

        impl.opened = false;
        impl.modelPath.clear();

        return srt::Expected<void>();
    }

    bool Session::isOpen() const {
        __stdc_impl_t;
        return impl.opened;
    }

    srt::Expected<srt::NO<srt::TaskResult>> Session::run(const srt::NO<srt::TaskStartInput> &input) {
        __stdc_impl_t;

        if (!impl.opened) {
            return srt::Error(srt::Error::SessionError, "session is not open");
        }

        auto startInput = input.as<Api::Common::SessionStartInput>();
        if (!startInput) {
            return srt::Error(srt::Error::InvalidArgument, "invalid task start input");
        }

        // TODO: Build ggml graph and run inference (Task 8)

        return srt::Error(srt::Error::NotImplemented, "ggml inference not yet implemented");
    }

    srt::NO<srt::TaskResult> Session::result() const {
        __stdc_impl_t;
        return impl.sessionResult.as<srt::TaskResult>();
    }

}
