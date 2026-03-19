#include "pch.h"
#include <Engine/Thread/Thread.h>
#include "PlatformThread.h"

namespace Zero
{
    void Thread::Join()
    {
        if (Joinable())
            m_thread.join();
    }

    bool Thread::Joinable() const
    {
        return m_thread.joinable();
    }

    ThreadId Thread::GetId() const
    {
        return m_thread.get_id();
    }

    ThreadId Thread::GetCurrentThreadId()
    {
        return std::this_thread::get_id();
    }

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
        // std::thread::native_handle() returns platform HANDLE/pthread_t
        // Cast through void* to keep PlatformThread.h free of platform headers
        return reinterpret_cast<void*>(m_thread.native_handle());
    }
}
