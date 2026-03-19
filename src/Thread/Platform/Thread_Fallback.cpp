#include "pch.h"
#include <Engine/Core/Platform.h>
#if !defined(PLATFORM_WINDOWS) && !defined(PLATFORM_LINUX) && \
    !defined(PLATFORM_ANDROID) && !defined(PLATFORM_MACOS)

#include "../PlatformThread.h"
#include <thread>
#include <chrono>

namespace Zero::PlatformThread
{
    void SetName(void*, std::string_view) {} // no portable API
    void SetPriority(void*, ThreadPriority) {} // no-op
    void SetAffinity(void*, uint64_t) {} // no-op

    void Sleep(uint32_t ms)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }

    void YieldT()
    {
        std::this_thread::yield();
    }
}
#endif
