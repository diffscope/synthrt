#ifndef SYNTHRT_SYNTHRT_GLOBAL_H
#define SYNTHRT_SYNTHRT_GLOBAL_H

#include <stdcorelib/stdc_global.h>

#ifndef SYNTHRT_EXPORT
#  ifdef SYNTHRT_STATIC
#    define SYNTHRT_EXPORT
#  else
#    ifdef SYNTHRT_LIBRARY
#      define SYNTHRT_EXPORT STDCORELIB_DECL_EXPORT
#    else
#      define SYNTHRT_EXPORT STDCORELIB_DECL_IMPORT
#    endif
#  endif
#endif

#endif // SYNTHRT_SYNTHRT_GLOBAL_H