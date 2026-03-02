#pragma once
#include <Engine/Core.h>

#if defined(ENGINE_ENABLE_PROFILING)

#include <tracy/Tracy.hpp>

#define ENGINE_PROFILE_FUNCTION() \
    ZoneScoped

#define ENGINE_PROFILE_SCOPE(name) \
    ZoneScopedN(name)
#define ENGINE_PROFILE_SCOPE_DYNAMIC(name, size) \
    ZoneScoped; \
    ZoneName(name, size)

#define ENGINE_PROFILE_FRAME() FrameMark
#define ENGINE_PROFILE_FRAME_NAME(name) FrameMarkNamed(name)

#define ENGINE_PROFILE_THREAD(name) tracy::SetThreadName(name)

#define ENGINE_PROFILE_PLOT(name, value) TracyPlot(name, value)

#define ENGINE_PROFILE_MESSAGE(text, size) TracyMessage(text, size)
#define ENGINE_PROFILE_MESSAGE_L(text) TracyMessageL(text)

#define ENGINE_PROFILE_ALLOC(ptr, size) TracyAlloc(ptr, size)
#define ENGINE_PROFILE_FREE(ptr) TracyFree(ptr)

#else

#define ENGINE_PROFILE_FUNCTION()
#define ENGINE_PROFILE_SCOPE(name)
#define ENGINE_PROFILE_SCOPE_DYNAMIC(name, size)
#define ENGINE_PROFILE_FRAME()
#define ENGINE_PROFILE_FRAME_NAME(name)
#define ENGINE_PROFILE_THREAD(name)
#define ENGINE_PROFILE_PLOT(name, value)
#define ENGINE_PROFILE_MESSAGE(text, size)
#define ENGINE_PROFILE_MESSAGE_L(text)
#define ENGINE_PROFILE_ALLOC(ptr, size)
#define ENGINE_PROFILE_FREE(ptr)

#endif
