#pragma once

#include <stdcorelib/stdc_global.h>

#ifndef SRT_S2P_EXPORT
#  ifdef SRT_S2P_STATIC
#    define SRT_S2P_EXPORT
#  else
#    ifdef SRT_S2P_LIBRARY
#      define SRT_S2P_EXPORT STDC_DECL_EXPORT
#    else
#      define SRT_S2P_EXPORT STDC_DECL_IMPORT
#    endif
#  endif
#endif
