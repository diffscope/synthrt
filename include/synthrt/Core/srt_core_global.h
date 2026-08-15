#pragma once

#include <stdcorelib/stdc_global.h>

#ifndef SRT_CORE_EXPORT
#  ifdef SRT_CORE_STATIC
#    define SRT_CORE_EXPORT
#  else
#    ifdef SRT_CORE_LIBRARY
#      define SRT_CORE_EXPORT STDC_DECL_EXPORT
#    else
#      define SRT_CORE_EXPORT STDC_DECL_IMPORT
#    endif
#  endif
#endif

#ifndef SYNTHRT_EXPORT
#  define SYNTHRT_EXPORT SRT_CORE_EXPORT
#endif
