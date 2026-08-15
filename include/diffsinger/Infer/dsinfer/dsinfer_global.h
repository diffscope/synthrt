#pragma once

#include <stdcorelib/stdc_global.h>

#ifndef DSINFER_EXPORT
#  ifdef DSINFER_STATIC
#    define DSINFER_EXPORT
#  else
#    ifdef DSINFER_LIBRARY
#      define DSINFER_EXPORT STDC_DECL_EXPORT
#    else
#      define DSINFER_EXPORT STDC_DECL_IMPORT
#    endif
#  endif
#endif
