#pragma once

#include <stdcorelib/stdc_global.h>

#ifndef DSSESSION_EXPORT
#  ifdef DSSESSION_STATIC
#    define DSSESSION_EXPORT
#  elif defined(DSSESSION_LIBRARY)
#    define DSSESSION_EXPORT STDC_DECL_EXPORT
#  else
#    define DSSESSION_EXPORT STDC_DECL_IMPORT
#  endif
#endif
