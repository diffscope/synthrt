#pragma once

#include <stdcorelib/stdc_global.h>

// v2 Phase 5: SVS types are now compiled into the independent srt-svs shared
// library (reversing v1 Phase 2 方案 A). The export macro follows the same
// pattern as SRT_CORE_EXPORT / SRT_S2P_EXPORT.
#ifndef SRT_SVS_EXPORT
#  ifdef SRT_SVS_STATIC
#    define SRT_SVS_EXPORT
#  else
#    ifdef SRT_SVS_LIBRARY
#      define SRT_SVS_EXPORT STDCORELIB_DECL_EXPORT
#    else
#      define SRT_SVS_EXPORT STDCORELIB_DECL_IMPORT
#    endif
#  endif
#endif
