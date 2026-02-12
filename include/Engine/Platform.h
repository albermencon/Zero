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
