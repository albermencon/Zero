#pragma once

// Platform Detection
#if defined(_WIN32) || defined(_WIN64)
    #define PLATFORM_WINDOWS
#elif defined(__ANDROID__)
    #define PLATFORM_ANDROID
#elif defined(__APPLE__) || defined(__MACH__)
    #include <TargetConditionals.h>
    #if TARGET_IPHONE_SIMULATOR == 1
        #define PLATFORM_IOS
    #elif TARGET_OS_IPHONE == 1
        #define PLATFORM_IOS
    #elif TARGET_OS_MAC == 1
        #define PLATFORM_MACOS
    #else
        #error "Unknown Apple platform."
    #endif
#elif defined(__linux__)
    #define PLATFORM_LINUX
#else
    #error "Unsupported platform."
#endif

// Debug Break
#ifdef PLATFORM_WINDOWS
    #include <intrin.h>
    #define DEBUGBREAK() __debugbreak()
#elif defined(PLATFORM_LINUX)
    #include <signal.h>
    #define DEBUGBREAK() raise(SIGTRAP)
#elif defined(PLATFORM_MACOS)
    #include <signal.h>
    #define DEBUGBREAK() raise(SIGTRAP)
#else
    #define DEBUGBREAK()
#endif

// Aligned heap allocation helpers
#ifdef PLATFORM_WINDOWS
    #include <malloc.h>
    #define ZR_ALIGNED_MALLOC(size, align) _aligned_malloc((size), (align))
    #define ZR_ALIGNED_FREE(ptr)           _aligned_free(ptr)
#else
    #include <cstdlib>
    #define ZR_ALIGNED_MALLOC(size, align) std::aligned_alloc((align), (size))
    #define ZR_ALIGNED_FREE(ptr)           std::free(ptr)
#endif

// Force inline
#if defined(_DEBUG) || !defined(__OPTIMIZE__)
    #define ZERO_FORCE_INLINE inline
#elif defined(_MSC_VER)
    #define ZERO_FORCE_INLINE inline __forceinline
#elif defined(__GNUC__) || defined(__clang__)
    #define ZERO_FORCE_INLINE inline __attribute__((always_inline))
#else
    #define ZERO_FORCE_INLINE inline
#endif
