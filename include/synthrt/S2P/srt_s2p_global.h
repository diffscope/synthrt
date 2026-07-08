#ifndef SRT_S2P_SRT_S2P_GLOBAL_H
#define SRT_S2P_SRT_S2P_GLOBAL_H

#include <stdcorelib/stdc_global.h>

#ifndef SRT_S2P_EXPORT
#  ifdef SRT_S2P_STATIC
#    define SRT_S2P_EXPORT
#  else
#    ifdef SRT_S2P_LIBRARY
#      define SRT_S2P_EXPORT STDCORELIB_DECL_EXPORT
#    else
#      define SRT_S2P_EXPORT STDCORELIB_DECL_IMPORT
#    endif
#  endif
#endif

#endif // SRT_S2P_SRT_S2P_GLOBAL_H
