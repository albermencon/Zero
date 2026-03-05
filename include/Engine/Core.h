#pragma once

#include "Platform.h"
#include "Export.h"

#ifdef ENGINE_DEBUG
    #define ENGINE_ENABLE_ASSERTS
#endif

#define ENGINE_ENABLE_ASSERTS
#define ENGINE_DEBUG

#ifdef ENGINE_ENABLE_ASSERTS
    #define ENGINE_ASSERT(x, ...) { if(!(x)) { ENGINE_ERROR("Assertion Failed: {0}", __VA_ARGS__); __debugbreak(); } }
    #define ENGINE_CORE_ASSERT(x, ...) { if(!(x)) { ENGINE_CORE_ERROR("Assertion Failed: {0}", __VA_ARGS__); __debugbreak(); } }
#else
    #define ENGINE_ASSERT(x, ...)
    #define ENGINE_CORE_ASSERT(x, ...)
#endif

