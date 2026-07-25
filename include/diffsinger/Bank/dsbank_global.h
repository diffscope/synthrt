#pragma once

#include <stdcorelib/stdc_global.h>

#ifndef DSBANK_EXPORT
#  ifdef DSBANK_STATIC
#    define DSBANK_EXPORT
#  else
#    ifdef DSBANK_LIBRARY
#      define DSBANK_EXPORT STDCORELIB_DECL_EXPORT
#    else
#      define DSBANK_EXPORT STDCORELIB_DECL_IMPORT
#    endif
#  endif
#endif
