#pragma once

namespace ds::bank {

    /// ResolutionState - Describes whether a package resource has been resolved
    /// on disk (e.g. a singer was found, or is still pending discovery, or is
    /// missing after resolution).
    enum class ResolutionState {
        Resolved,
        Pending,
        Missing,
    };

}
