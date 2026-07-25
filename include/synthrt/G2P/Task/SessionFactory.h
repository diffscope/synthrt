#pragma once

#include <string>

#include <synthrt/Core/Core/NamedObject.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/G2P/srt_g2p_global.h>
#include <synthrt/G2P/Task/Task.h>

namespace srt::g2p {

    /// SessionFactory - G2P inference driver interface.
    ///
    /// Migrated from LangCore::SessionFactory. An instance of SessionFactory
    /// needs to be added to the G2P driver category with the ID
    /// \c kG2pOnnxDriverName ("g2pOnnxDriver") before it can be called by
    /// G2P inference tasks.
    class SRT_G2P_EXPORT SessionFactory : public srt::core::NamedObject {
    public:
        ~SessionFactory() override;

        /// Related arch.
        virtual std::string arch() const = 0;

        /// Driver backend identifier.
        virtual std::string backend() const = 0;

        virtual srt::core::Expected<void> initialize(const srt::core::NO<TaskInitArgs> &args) = 0;

        virtual srt::core::NO<SessionTask> createSession() = 0;
    };

} // namespace srt::g2p
