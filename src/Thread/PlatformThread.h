#pragma once
#include <string_view>
#include <cstdint>
#include <Engine/Thread/ThreadPriority.h>

// Internal platform abstraction
namespace Zero::PlatformThread
{
    void SetName(void* nativeHandle, std::string_view name);
    void SetPriority(void* nativeHandle, ThreadPriority priority);
    void SetAffinity(void* nativeHandle, uint64_t mask);
    void Sleep(uint32_t ms);
    void YieldT();
}
