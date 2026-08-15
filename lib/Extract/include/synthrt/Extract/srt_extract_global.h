#pragma once

#include <stdcorelib/stdc_global.h>

#ifndef SRT_EXTRACT_EXPORT
#  ifdef SRT_EXTRACT_STATIC
#    define SRT_EXTRACT_EXPORT
#  else
#    ifdef SRT_EXTRACT_LIBRARY
#      define SRT_EXTRACT_EXPORT STDC_DECL_EXPORT
#    else
#      define SRT_EXTRACT_EXPORT STDC_DECL_IMPORT
#    endif
#  endif
#endif
