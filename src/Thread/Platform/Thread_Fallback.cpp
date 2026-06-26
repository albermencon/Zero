#include "pch.h"
#include <Engine/Platform.h>
#if !defined(PLATFORM_WINDOWS) && !defined(PLATFORM_LINUX) && \
    !defined(PLATFORM_ANDROID) && !defined(PLATFORM_MACOS)

#include <Engine/Thread/Thread.h>
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

namespace Zero
{
    Thread::~Thread()
    {
        Detach();
    }

    void Thread::Start(size_t /*stackSize*/, void*(*threadFunc)(void*), void* param)
    {
        std::thread* t = new std::thread([threadFunc, param]() {
            threadFunc(param);
        });
        m_handle = t;
        m_id = std::hash<std::thread::id>{}(t->get_id());
    }

    void Thread::Detach()
    {
        if (Joinable())
        {
            std::thread* t = static_cast<std::thread*>(m_handle);
            t->detach();
            delete t;
            m_handle = nullptr;
            m_id = 0;
        }
    }

    void Thread::Join()
    {
        if (Joinable())
        {
            std::thread* t = static_cast<std::thread*>(m_handle);
            t->join();
            delete t;
            m_handle = nullptr;
            m_id = 0;
        }
    }

    bool Thread::Joinable() const
    {
        return m_handle != nullptr;
    }

    ThreadId Thread::GetId() const
    {
        return m_id;
    }

    ThreadId Thread::GetCurrentThreadId()
    {
        return std::hash<std::thread::id>{}(std::this_thread::get_id());
    }
}
#endif
