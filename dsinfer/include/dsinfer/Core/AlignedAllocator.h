#ifndef DSINFER_ALIGNEDALLOCATOR_H
#define DSINFER_ALIGNEDALLOCATOR_H

#include <cstddef>
#include <cstdlib>
#include <limits>
#include <new>

namespace ds {

    template <typename T, std::size_t Alignment>
    class AlignedAllocator {
    public:
        static_assert(
            Alignment >= alignof(void*) &&
            (Alignment % alignof(void*) == 0) &&
            (Alignment & (Alignment - 1)) == 0,
            "Alignment must be a power of two and a multiple of alignof(void*)"
        );

        using value_type = T;
        using pointer = T *;
        using const_pointer = const T *;
        using reference = T &;
        using const_reference = const T &;
        using size_type = std::size_t;
        using difference_type = std::ptrdiff_t;

        using propagate_on_container_move_assignment = std::true_type;
        using is_always_equal = std::true_type;

        AlignedAllocator() noexcept = default;

        template <typename U>
        AlignedAllocator(const AlignedAllocator<U, Alignment> &) noexcept {
        }

        [[nodiscard]]
        T *allocate(size_type n) {
            if (n > max_size()) {
                throw std::bad_alloc();
            }

            const size_type size = n * sizeof(T);
            void *ptr = aligned_alloc_impl(size, Alignment);
            if (!ptr) {
                throw std::bad_alloc();
            }
            return static_cast<T *>(ptr);
        }

        void deallocate(T *p, size_type /*n*/) noexcept {
            aligned_free_impl(p);
        }

        size_type max_size() const noexcept {
            return std::numeric_limits<size_type>::max() / sizeof(T);
        }

        template <typename U>
        struct rebind {
            using other = AlignedAllocator<U, Alignment>;
        };

        template <typename T1, size_type A1, typename T2, size_type A2>
        friend constexpr bool operator==(const AlignedAllocator<T1, A1> &,
                                         const AlignedAllocator<T2, A2> &) noexcept;

        template <typename T1, size_type A1, typename T2, size_type A2>
        friend constexpr bool operator!=(const AlignedAllocator<T1, A1> &,
                                         const AlignedAllocator<T2, A2> &) noexcept;

    private:
        static inline void *aligned_alloc_impl(size_type size, size_type alignment) {
#if defined(_MSC_VER)
            return ::_aligned_malloc(size, alignment);

#elif defined(__MINGW32__) || defined(__MINGW64__)
            return ::__mingw_aligned_malloc(size, alignment);

#elif defined(__APPLE__) || defined(__unix__) || defined(__linux__) || defined(_POSIX_VERSION)
            void *ptr = nullptr;
            if (::posix_memalign(&ptr, alignment, size) != 0)
                return nullptr;
            return ptr;

#elif defined(__cpp_aligned_new)
            if ((size % alignment) != 0) {
                size = ((size + alignment - 1) / alignment) * alignment;
            }
            return std::aligned_alloc(alignment, size);

#else
#  error "AlignedAllocator: Unsupported platform"
#endif
        }

        static inline void aligned_free_impl(void *p) noexcept {
#if defined(_MSC_VER)
            ::_aligned_free(p);

#elif defined(__MINGW32__) || defined(__MINGW64__)
            ::__mingw_aligned_free(p);

#elif defined(__APPLE__) || defined(__unix__) || defined(__linux__) || defined(_POSIX_VERSION)
            ::free(p);

#elif defined(__cpp_aligned_new)
            std::free(p);

#else
#  error "AlignedAllocator: Unsupported platform"
#endif
        }
    };

    template <typename T1, std::size_t A1, typename T2, std::size_t A2>
    constexpr bool operator==(const AlignedAllocator<T1, A1> &,
                              const AlignedAllocator<T2, A2> &) noexcept {
        return A1 == A2;
    }

    template <typename T1, std::size_t A1, typename T2, std::size_t A2>
    constexpr bool operator!=(const AlignedAllocator<T1, A1> &a,
                              const AlignedAllocator<T2, A2> &b) noexcept {
        return !(a == b);
    }

}

#endif // DSINFER_ALIGNEDALLOCATOR_H
