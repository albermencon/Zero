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
