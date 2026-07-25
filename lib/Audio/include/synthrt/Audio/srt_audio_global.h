#pragma once

#include <stdcorelib/stdc_global.h>

#ifndef SRT_AUDIO_EXPORT
#  ifdef SRT_AUDIO_STATIC
#    define SRT_AUDIO_EXPORT
#  else
#    ifdef SRT_AUDIO_LIBRARY
#      define SRT_AUDIO_EXPORT STDCORELIB_DECL_EXPORT
#    else
#      define SRT_AUDIO_EXPORT STDCORELIB_DECL_IMPORT
#    endif
#  endif
#endif
