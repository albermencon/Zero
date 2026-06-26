#include "pch.h"
#include <Engine/Core.h>
#ifdef PLATFORM_MACOS

#include <Engine/Thread/Thread.h>
#include "../PlatformThread.h"
#include <pthread.h>
#include <mach/mach.h>
#include <mach/thread_policy.h>
#include <cstring>
#include <algorithm>

namespace Zero::PlatformThread
{
    void SetName(void* /*nativeHandle*/, std::string_view name)
    {
        // macOS: pthread_setname_np only sets name for the *calling* thread
        // Call from within the thread itself — store name and call from thread body
        char buf[64];
        size_t len = std::min(name.size(), static_cast<size_t>(63));
        std::memcpy(buf, name.data(), len);
        buf[len] = '\0';
        pthread_setname_np(buf); // no handle param on macOS
    }

    void SetPriority(void* nativeHandle, ThreadPriority priority)
    {
        // macOS uses Mach thread scheduling
        mach_port_t mach_thread = pthread_mach_thread_np(
            reinterpret_cast<pthread_t>(nativeHandle));

        thread_precedence_policy_data_t policy{};
        switch (priority)
        {
        case ThreadPriority::Low:      policy.importance = 0;   break;
        case ThreadPriority::Normal:   policy.importance = 16;  break;
        case ThreadPriority::High:     policy.importance = 32;  break;
        case ThreadPriority::Critical: policy.importance = 63;  break;
        }
        thread_policy_set(mach_thread, THREAD_PRECEDENCE_POLICY,
            reinterpret_cast<thread_policy_t>(&policy),
            THREAD_PRECEDENCE_POLICY_COUNT);
    }

    void SetAffinity(void* /*nativeHandle*/, uint64_t /*mask*/)
    {
        // macOS does not expose per-thread CPU affinity to userspace.
        // Affinity hints exist (THREAD_AFFINITY_POLICY) but are advisory only.
        // No-op is correct here.
    }

    void Sleep(uint32_t ms)
    {
        struct timespec ts { ms / 1000, static_cast<long>(ms % 1000) * 1'000'000L };
        while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {}
    }

    void YieldT()
    {
        sched_yield();
    }
}

namespace Zero
{
    Thread::~Thread()
    {
        Detach();
    }

    void Thread::Start(size_t stackSize, void*(*threadFunc)(void*), void* param)
    {
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        if (stackSize > 0)
        {
            size_t minStack = 16384;
#ifdef PTHREAD_STACK_MIN
            minStack = PTHREAD_STACK_MIN;
#endif
            if (stackSize < minStack) stackSize = minStack;

            pthread_attr_setstacksize(&attr, stackSize);
        }

        pthread_t thread;
        int result = pthread_create(&thread, &attr, threadFunc, param);
        pthread_attr_destroy(&attr);

        if (result == 0)
        {
            m_handle = reinterpret_cast<void*>(thread);
            m_id = static_cast<uint64_t>(thread);
        }
        else
        {
            m_handle = nullptr;
            m_id = 0;
        }
    }

    void Thread::Detach()
    {
        if (Joinable())
        {
            pthread_detach(reinterpret_cast<pthread_t>(m_handle));
            m_handle = nullptr;
            m_id = 0;
        }
    }

    void Thread::Join()
    {
        if (Joinable())
        {
            pthread_join(reinterpret_cast<pthread_t>(m_handle), nullptr);
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
        return static_cast<uint64_t>(pthread_self());
    }
}
#endif
