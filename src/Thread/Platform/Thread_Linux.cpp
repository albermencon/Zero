#include "pch.h"
#include <Engine/Core.h>
#if defined(PLATFORM_LINUX) || defined(PLATFORM_ANDROID)

#include <Engine/Thread/Thread.h>
#include <Engine/Thread/Mutex.h>
#include <Engine/Thread/RecursiveMutex.h>
#include <Engine/Thread/ConditionVariable.h>
#include <Engine/Thread/Semaphore.h>
#include "../PlatformThread.h"
#include <pthread.h>
#include <semaphore.h>
#include <sched.h>
#include <time.h>
#include <cstring>
#include <unistd.h>
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
        bool setStack = false;
        if (stackSize > 0)
        {
            size_t minStack = 16384;
#ifdef PTHREAD_STACK_MIN
            minStack = PTHREAD_STACK_MIN;
#endif
            long sysMin = sysconf(_SC_THREAD_STACK_MIN);
            if (sysMin > 0 && static_cast<size_t>(sysMin) > minStack)
                minStack = static_cast<size_t>(sysMin);

            if (stackSize < minStack) stackSize = minStack;

            long pageSize = sysconf(_SC_PAGESIZE);
            if (pageSize > 0)
                stackSize = (stackSize + pageSize - 1) & ~(pageSize - 1);

            if (pthread_attr_setstacksize(&attr, stackSize) == 0)
                setStack = true;
        }

        pthread_t thread;
        int result = pthread_create(&thread, &attr, threadFunc, param);
        
        if (result == EINVAL && setStack)
        {
            pthread_attr_destroy(&attr);
            pthread_attr_init(&attr);
            result = pthread_create(&thread, &attr, threadFunc, param);
        }
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

    static_assert(sizeof(pthread_mutex_t) <= 64, "pthread_mutex_t size exceeds storage buffer");
    static_assert(alignof(pthread_mutex_t) <= 8, "pthread_mutex_t alignment exceeds storage buffer");

    Mutex::Mutex()
    {
        pthread_mutex_init(reinterpret_cast<pthread_mutex_t*>(m_storage), nullptr);
    }

    Mutex::~Mutex()
    {
        pthread_mutex_destroy(reinterpret_cast<pthread_mutex_t*>(m_storage));
    }
    
    void Mutex::Lock() { pthread_mutex_lock(reinterpret_cast<pthread_mutex_t*>(m_storage)); }
    void Mutex::Unlock() { pthread_mutex_unlock(reinterpret_cast<pthread_mutex_t*>(m_storage)); }
    bool Mutex::TryLock() { return pthread_mutex_trylock(reinterpret_cast<pthread_mutex_t*>(m_storage)) == 0; }

    RecursiveMutex::RecursiveMutex()
    {
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_init(reinterpret_cast<pthread_mutex_t*>(m_storage), &attr);
        pthread_mutexattr_destroy(&attr);
    }

    RecursiveMutex::~RecursiveMutex()
    {
        pthread_mutex_destroy(reinterpret_cast<pthread_mutex_t*>(m_storage));
    }

    void RecursiveMutex::Lock() { pthread_mutex_lock(reinterpret_cast<pthread_mutex_t*>(m_storage)); }
    void RecursiveMutex::Unlock() { pthread_mutex_unlock(reinterpret_cast<pthread_mutex_t*>(m_storage)); }
    bool RecursiveMutex::TryLock() { return pthread_mutex_trylock(reinterpret_cast<pthread_mutex_t*>(m_storage)) == 0; }

    static_assert(sizeof(pthread_cond_t) <= 48, "pthread_cond_t size exceeds storage buffer");
    static_assert(alignof(pthread_cond_t) <= 8, "pthread_cond_t alignment exceeds storage buffer");

    ConditionVariable::ConditionVariable()
    {
        pthread_cond_init(reinterpret_cast<pthread_cond_t*>(m_storage), nullptr);
    }

    ConditionVariable::~ConditionVariable()
    {
        pthread_cond_destroy(reinterpret_cast<pthread_cond_t*>(m_storage));
    }

    void ConditionVariable::Wait(Mutex& mutex)
    {
        pthread_cond_wait(reinterpret_cast<pthread_cond_t*>(m_storage), reinterpret_cast<pthread_mutex_t*>(mutex.GetNativeHandle()));
    }
    
    void ConditionVariable::NotifyOne()
    {
        pthread_cond_signal(reinterpret_cast<pthread_cond_t*>(m_storage));
    }

    void ConditionVariable::NotifyAll()
    {
        pthread_cond_broadcast(reinterpret_cast<pthread_cond_t*>(m_storage));
    }

    static_assert(sizeof(sem_t) <= 32, "sem_t size exceeds storage buffer");
    static_assert(alignof(sem_t) <= 8, "sem_t alignment exceeds storage buffer");

    Semaphore::Semaphore(int initialCount)
    {
        sem_init(reinterpret_cast<sem_t*>(m_storage), 0, static_cast<unsigned int>(initialCount));
    }

    Semaphore::~Semaphore()
    {
        sem_destroy(reinterpret_cast<sem_t*>(m_storage));
    }

    void Semaphore::Wait()
    {
        while (sem_wait(reinterpret_cast<sem_t*>(m_storage)) == -1 && errno == EINTR) {}
    }

    void Semaphore::Signal()
    {
        sem_post(reinterpret_cast<sem_t*>(m_storage));
    }
}
#endif
