#ifndef SYNTHRT_SYNTHRT_GLOBAL_H
#define SYNTHRT_SYNTHRT_GLOBAL_H

#include <type_traits>

#include <stdcorelib/stdc_global.h>

#ifndef SYNTHRT_EXPORT
#  ifdef SYNTHRT_STATIC
#    define SYNTHRT_EXPORT
#  else
#    ifdef SYNTHRT_LIBRARY
#      define SYNTHRT_EXPORT STDC_DECL_EXPORT
#    else
#      define SYNTHRT_EXPORT STDC_DECL_IMPORT
#    endif
#  endif
#endif

#define SYNTHRT_DECLARE_AS_METHODS(Base)                                                           \
    template <class T>                                                                             \
    T *as() {                                                                                      \
        static_assert(std::is_base_of_v<Base, T>, "T must derive from " #Base);                    \
        return static_cast<T *>(this);                                                             \
    }                                                                                              \
    template <class T>                                                                             \
    const T *as() const {                                                                          \
        static_assert(std::is_base_of_v<Base, T>, "T must derive from " #Base);                    \
        return static_cast<const T *>(this);                                                       \
    }

#endif // SYNTHRT_SYNTHRT_GLOBAL_H
