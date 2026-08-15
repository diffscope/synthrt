#pragma once

#include <stdcorelib/stdc_global.h>

#ifndef SRT_DRIVER_EXPORT
#  ifdef SRT_DRIVER_STATIC
#    define SRT_DRIVER_EXPORT
#  else
#    ifdef SRT_DRIVER_LIBRARY
#      define SRT_DRIVER_EXPORT STDC_DECL_EXPORT
#    else
#      define SRT_DRIVER_EXPORT STDC_DECL_IMPORT
#    endif
#  endif
#endif
