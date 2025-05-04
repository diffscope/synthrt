#ifndef DSINFER_DSINFER_GLOBAL_H
#define DSINFER_DSINFER_GLOBAL_H

#include <stdcorelib/stdc_global.h>

#ifndef DSINFER_EXPORT
#  ifdef DSINFER_STATIC
#    define DSINFER_EXPORT
#  else
#    ifdef DSINFER_LIBRARY
#      define DSINFER_EXPORT STDCORELIB_DECL_EXPORT
#    else
#      define DSINFER_EXPORT STDCORELIB_DECL_IMPORT
#    endif
#  endif
#endif

#endif // DSINFER_DSINFER_GLOBAL_H
