#pragma once
#include <Engine/Core.h>

#if defined(ZERO_ENABLE_PROFILING)

#include <tracy/Tracy.hpp>

#define ZERO_PROFILE_FUNCTION() \
    ZoneScoped

#define ZERO_PROFILE_SCOPE(name) \
    ZoneScopedN(name)
#define ZERO_PROFILE_SCOPE_DYNAMIC(name, size) \
    ZoneScoped; \
    ZoneName(name, size)

#define ZERO_PROFILE_FRAME() FrameMark
#define ZERO_PROFILE_FRAME_NAME(name) FrameMarkNamed(name)

#define ZERO_PROFILE_THREAD(name) tracy::SetThreadName(name)

#define ZERO_PROFILE_PLOT(name, value) TracyPlot(name, value)

#define ZERO_PROFILE_MESSAGE(text, size) TracyMessage(text, size)
#define ZERO_PROFILE_MESSAGE_L(text) TracyMessageL(text)

#define ZERO_PROFILE_ALLOC(ptr, size) TracyAlloc(ptr, size)
#define ZERO_PROFILE_FREE(ptr) TracyFree(ptr)

#else

#define ZERO_PROFILE_FUNCTION()
#define ZERO_PROFILE_SCOPE(name)
#define ZERO_PROFILE_SCOPE_DYNAMIC(name, size)
#define ZERO_PROFILE_FRAME()
#define ZERO_PROFILE_FRAME_NAME(name)
#define ZERO_PROFILE_THREAD(name)
#define ZERO_PROFILE_PLOT(name, value)
#define ZERO_PROFILE_MESSAGE(text, size)
#define ZERO_PROFILE_MESSAGE_L(text)
#define ZERO_PROFILE_ALLOC(ptr, size)
#define ZERO_PROFILE_FREE(ptr)

#endif
