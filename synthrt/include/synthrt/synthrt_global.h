#ifndef SYNTHRT_SYNTHRT_GLOBAL_H
#define SYNTHRT_SYNTHRT_GLOBAL_H

#include <cassert>
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

// The downcast helpers below are a static_cast, because the caller has already compared the
// contract carried by the object. A derived class that states the contract of another class would
// pass that comparison and make the cast undefined behavior, so a debug build verifies the dynamic
// type as well. A build without RTTI keeps the contract comparison as its only guard.
#if defined(_CPPRTTI) || defined(__GXX_RTTI) || defined(__cpp_rtti)
#  define SYNTHRT_DETAIL_ASSERT_DYNAMIC_TYPE(Type)                                                 \
      assert(dynamic_cast<const Type *>(this) != nullptr)
#else
#  define SYNTHRT_DETAIL_ASSERT_DYNAMIC_TYPE(Type) static_cast<void>(0)
#endif

#define SYNTHRT_DECLARE_AS_METHODS(Base)                                                           \
    template <class T>                                                                             \
    T *as() {                                                                                      \
        static_assert(std::is_base_of_v<Base, T>, "T must derive from " #Base);                    \
        SYNTHRT_DETAIL_ASSERT_DYNAMIC_TYPE(T);                                                     \
        return static_cast<T *>(this);                                                             \
    }                                                                                              \
    template <class T>                                                                             \
    const T *as() const {                                                                          \
        static_assert(std::is_base_of_v<Base, T>, "T must derive from " #Base);                    \
        SYNTHRT_DETAIL_ASSERT_DYNAMIC_TYPE(T);                                                     \
        return static_cast<const T *>(this);                                                       \
    }

#endif // SYNTHRT_SYNTHRT_GLOBAL_H
