#pragma once

// Legacy location — migrated to <synthrt/G2P/LanguageRoute.h> (R8).
// This header is kept as a forwarding shim for external consumers (e.g.
// ds-editor-lite) during the namespace migration window. New code should
// include <synthrt/G2P/LanguageRoute.h> directly and use namespace srt::g2p.
//
// Note: the underlying struct now uses the renamed fields (g2pContext /
// g2pSource) defined in srt::g2p::LanguageRoute. Callers still referencing
// the old field names (singerId / voicebankContext) must be updated.

#include <synthrt/G2P/LanguageRoute.h>

namespace ds::lang {
    /// Type alias for backward compatibility with the old include path.
    /// New code should use srt::g2p::LanguageRoute directly.
    using LanguageRoute = srt::g2p::LanguageRoute;
} // namespace ds::lang
