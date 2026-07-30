#pragma once

#include "Platform.h"
#include "Export.h"

#ifdef ZERO_DEBUG
    #define ZERO_ENABLE_ASSERTS
#endif

#define ZERO_ENABLE_ASSERTS
#define ZERO_DEBUG

#ifdef ZERO_ENABLE_ASSERTS
    #define CLIENT_CORE_ASSERT(x, ...) { if (!(x)) { CLIENT_CORE_ERROR("Assertion Failed: {0}", __VA_ARGS__); DEBUGBREAK(); } }
    #define CLIENT_ASSERT(x, ...) { if(!(x)) { CLIENT_ERROR("Assertion Failed: {0}", __VA_ARGS__); DEBUGBREAK(); } }
    #define ZERO_CORE_ASSERT(x, ...) { if(!(x)) { ZERO_CORE_CRITICAL("Assertion Failed: {0}", __VA_ARGS__); DEBUGBREAK(); } }
    #define ZERO_ASSERT(x, ...) { if(!(x)) { ZERO_CORE_ERROR("Assertion Failed: {0}", __VA_ARGS__); DEBUGBREAK(); } }
    #define ECS_VALIDATE
#else
    #define CLIENT_CORE_ASSERT(x, ...)
    #define CLIENT_ASSERT(x, ...)
    #define ZERO_CORE_ASSERT(x, ...)
    #define ZERO_ASSERT(x, ...)
#endif
