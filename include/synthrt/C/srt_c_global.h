#pragma once

#include <stdcorelib/stdc_global.h>

#ifndef SRT_C_EXPORT
#  ifdef SRT_C_STATIC
#    define SRT_C_EXPORT
#  else
#    ifdef SRT_C_LIBRARY
#      define SRT_C_EXPORT STDCORELIB_DECL_EXPORT
#    else
#      define SRT_C_EXPORT STDCORELIB_DECL_IMPORT
#    endif
#  endif
#endif
