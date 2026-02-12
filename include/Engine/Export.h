#pragma once
#include "Platform.h"

#if defined(PLATFORM_WINDOWS)
    #if defined(BUILD_SHARED)
        #define ENGINE_API __declspec(dllexport) 
    #elif defined(USE_SHARED)
        #define ENGINE_API __declspec(dllimport)
    #else
        #define ENGINE_API
    #endif
#else
    #if defined(BUILD_SHARED)
        #define ENGINE_API __attribute__((visibility("default")))
    #else
        #define ENGINE_API
    #endif
#endif
