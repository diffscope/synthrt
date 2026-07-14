// srt_driver.cpp - Out-of-line definitions for srt-driver dllexport classes.
//
// MSVC does not export implicitly-generated constructors/destructors or inline
// virtual functions from a DLL even when the class is marked __declspec(dllexport).
// These definitions force the symbols to be emitted into the shared library so
// that plugins linking against srt::driver can resolve them.

#include <synthrt/Driver/InferenceDriver.h>
#include <synthrt/Driver/InferenceDriverPlugin.h>
#include <synthrt/Driver/InferenceSession.h>

namespace srt::driver {

    // --- InferenceSessionResult ---
    InferenceSessionResult::InferenceSessionResult(std::string name, int version)
        : srt::core::TaskResult(std::move(name)), version(version) {
    }

    InferenceSessionResult::~InferenceSessionResult() = default;

    // --- InferenceSession ---
    InferenceSession::InferenceSession() = default;
    InferenceSession::~InferenceSession() = default;

    std::vector<std::string> InferenceSession::inputNames() const {
        return {};
    }

    // --- InferenceDriverPlugin ---
    InferenceDriverPlugin::InferenceDriverPlugin() = default;
    InferenceDriverPlugin::~InferenceDriverPlugin() = default;

}
