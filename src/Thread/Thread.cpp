#include "pch.h"
#include <Engine/Thread/Thread.h>
#include "PlatformThread.h"

namespace Zero
{
    void Thread::SetName(std::string_view name)
    {
        m_name = name;
        PlatformThread::SetName(NativeHandle(), name);
    }

    void Thread::SetPriority(ThreadPriority priority)
    {
        PlatformThread::SetPriority(NativeHandle(), priority);
    }

    void Thread::SetAffinity(uint64_t mask)
    {
        PlatformThread::SetAffinity(NativeHandle(), mask);
    }

    void Thread::Sleep(uint32_t ms)
    {
        PlatformThread::Sleep(ms);
    }

    void Thread::Yield()
    {
        PlatformThread::YieldT();
    }

    void* Thread::NativeHandle()
    {
        return m_handle;
    }
}
