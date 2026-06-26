#include "pch.h"
#include <Engine/Core.h>
#if defined(PLATFORM_LINUX) || defined(PLATFORM_ANDROID)

#include <Engine/Thread/Thread.h>
#include "../PlatformThread.h"
#include <pthread.h>
#include <sched.h>
#include <time.h>
#include <cstring>
#include <algorithm>
#include <string>

namespace Zero::PlatformThread
{
    void SetName(void* nativeHandle, std::string_view name)
    {
        pthread_t handle = reinterpret_cast<pthread_t>(nativeHandle);
        if (!handle) handle = pthread_self();

        // pthread limit: 15 chars + null
        char buf[16];
        size_t len = std::min(name.size(), static_cast<size_t>(15));
        std::memcpy(buf, name.data(), len);
        buf[len] = '\0';

        pthread_setname_np(handle, buf);
    }

    void SetPriority(void* nativeHandle, ThreadPriority priority)
    {
        pthread_t handle = reinterpret_cast<pthread_t>(nativeHandle);
        if (!handle) handle = pthread_self();

        // SCHED_OTHER (default CFS) ignores sched_priority — use nice-like approach.
        // For real-time: SCHED_RR with priority 1-99.
        // Critical → SCHED_RR; High → SCHED_RR low; others → SCHED_OTHER + nice
        sched_param param{};
        int policy;

        switch (priority)
        {
        case ThreadPriority::Critical:
            policy = SCHED_RR;
            param.sched_priority = sched_get_priority_max(SCHED_RR);
            break;
        case ThreadPriority::High:
            policy = SCHED_RR;
            param.sched_priority = sched_get_priority_min(SCHED_RR) + 1;
            break;
        case ThreadPriority::Normal:
        case ThreadPriority::Low:
        default:
            policy = SCHED_OTHER;
            param.sched_priority = 0; // must be 0 for SCHED_OTHER
            break;
        }

        // Requires CAP_SYS_NICE or appropriate rlimit for RT policies
        pthread_setschedparam(handle, policy, &param);
    }

    void SetAffinity(void* nativeHandle, uint64_t mask)
    {
        pthread_t handle = reinterpret_cast<pthread_t>(nativeHandle);
        if (!handle) handle = pthread_self();
        
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        for (int i = 0; i < 64; ++i)
        {
            if (mask & (UINT64_C(1) << i))
                CPU_SET(i, &cpuset);
        }
        pthread_setaffinity_np(handle, sizeof(cpu_set_t), &cpuset);
    }

    void Sleep(uint32_t ms)
    {
        struct timespec ts;
        ts.tv_sec = ms / 1000;
        ts.tv_nsec = static_cast<long>(ms % 1000) * 1'000'000L;
        // Handle EINTR — restart if interrupted by signal
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
