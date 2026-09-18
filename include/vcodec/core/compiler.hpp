// The only header permitted to contain a compiler #if (spec §2.1, §3 rule 3).
#ifndef VCODEC_CORE_COMPILER_HPP
#define VCODEC_CORE_COMPILER_HPP

#if defined(__clang__)
#  define VCODEC_COMPILER_CLANG 1
#  define VCODEC_COMPILER_GCC 0
#elif defined(__GNUC__)
#  define VCODEC_COMPILER_CLANG 0
#  define VCODEC_COMPILER_GCC 1
#else
#  error "vcodec requires GCC 16.1+ (-freflection) or Bloomberg clang-p2996"
#endif

#if !defined(__cpp_impl_reflection) && !__has_include(<meta>)
#  error "vcodec requires C++26 reflection: compile with -std=c++26 -freflection"
#endif

#if defined(__has_builtin)
#  if __has_builtin(__builtin_expect)
#    define VCODEC_LIKELY(x)   __builtin_expect(!!(x), 1)
#    define VCODEC_UNLIKELY(x) __builtin_expect(!!(x), 0)
#  endif
#endif
#ifndef VCODEC_LIKELY
#  define VCODEC_LIKELY(x)   (x)
#  define VCODEC_UNLIKELY(x) (x)
#endif

#if VCODEC_COMPILER_GCC || VCODEC_COMPILER_CLANG
#  define VCODEC_ALWAYS_INLINE [[gnu::always_inline]] inline
#else
#  define VCODEC_ALWAYS_INLINE inline
#endif

#endif // VCODEC_CORE_COMPILER_HPP
