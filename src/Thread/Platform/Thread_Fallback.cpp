#include "pch.h"
#include <Engine/Platform.h>
#if !defined(PLATFORM_WINDOWS) && !defined(PLATFORM_LINUX) && \
    !defined(PLATFORM_ANDROID) && !defined(PLATFORM_MACOS)

#include <Engine/Thread/Thread.h>
#include <Engine/Thread/Mutex.h>
#include <Engine/Thread/RecursiveMutex.h>
#include <Engine/Thread/ConditionVariable.h>
#include <Engine/Thread/Semaphore.h>
#include "../PlatformThread.h"
#include <thread>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <cstring>
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

    Mutex::Mutex()
    {
        std::mutex* mtx = new std::mutex();
        std::memcpy(m_storage, &mtx, sizeof(mtx));
    }

    Mutex::~Mutex()
    {
        std::mutex* mtx;
        std::memcpy(&mtx, m_storage, sizeof(mtx));
        delete mtx;
    }

    void Mutex::Lock()
    {
        std::mutex* mtx;
        std::memcpy(&mtx, m_storage, sizeof(mtx));
        mtx->lock();
    }

    void Mutex::Unlock()
    {
        std::mutex* mtx;
        std::memcpy(&mtx, m_storage, sizeof(mtx));
        mtx->unlock();
    }

    bool Mutex::TryLock()
    {
        std::mutex* mtx;
        std::memcpy(&mtx, m_storage, sizeof(mtx));
        return mtx->try_lock();
    }

    RecursiveMutex::RecursiveMutex()
    {
        std::recursive_mutex* mtx = new std::recursive_mutex();
        std::memcpy(m_storage, &mtx, sizeof(mtx));
    }

    RecursiveMutex::~RecursiveMutex()
    {
        std::recursive_mutex* mtx;
        std::memcpy(&mtx, m_storage, sizeof(mtx));
        delete mtx;
    }

    void RecursiveMutex::Lock()
    {
        std::recursive_mutex* mtx;
        std::memcpy(&mtx, m_storage, sizeof(mtx));
        mtx->lock();
    }

    void RecursiveMutex::Unlock()
    {
        std::recursive_mutex* mtx;
        std::memcpy(&mtx, m_storage, sizeof(mtx));
        mtx->unlock();
    }

    bool RecursiveMutex::TryLock()
    {
        std::recursive_mutex* mtx;
        std::memcpy(&mtx, m_storage, sizeof(mtx));
        return mtx->try_lock();
    }

    ConditionVariable::ConditionVariable()
    {
        std::condition_variable_any* cv = new std::condition_variable_any();
        std::memcpy(m_storage, &cv, sizeof(cv));
    }

    ConditionVariable::~ConditionVariable()
    {
        std::condition_variable_any* cv;
        std::memcpy(&cv, m_storage, sizeof(cv));
        delete cv;
    }

    void ConditionVariable::Wait(Mutex& mutex)
    {
        std::condition_variable_any* cv;
        std::memcpy(&cv, m_storage, sizeof(cv));
        std::mutex* mtx;
        std::memcpy(&mtx, mutex.GetNativeHandle(), sizeof(mtx));
        std::unique_lock<std::mutex> lock(*mtx, std::adopt_lock);
        cv->wait(lock);
        lock.release();
    }

    void ConditionVariable::NotifyOne()
    {
        std::condition_variable_any* cv;
        std::memcpy(&cv, m_storage, sizeof(cv));
        cv->notify_one();
    }

    void ConditionVariable::NotifyAll()
    {
        std::condition_variable_any* cv;
        std::memcpy(&cv, m_storage, sizeof(cv));
        cv->notify_all();
    }

    struct FallbackSemaphore
    {
        std::mutex mtx;
        std::condition_variable cv;
        int count;
    };

    Semaphore::Semaphore(int initialCount)
    {
        FallbackSemaphore* sem = new FallbackSemaphore{ {}, {}, initialCount };
        std::memcpy(m_storage, &sem, sizeof(sem));
    }

    Semaphore::~Semaphore()
    {
        FallbackSemaphore* sem;
        std::memcpy(&sem, m_storage, sizeof(sem));
        delete sem;
    }

    void Semaphore::Wait()
    {
        FallbackSemaphore* sem;
        std::memcpy(&sem, m_storage, sizeof(sem));
        std::unique_lock<std::mutex> lock(sem->mtx);
        sem->cv.wait(lock, [sem] { return sem->count > 0; });
        --sem->count;
    }
    
    void Semaphore::Signal()
    {
        FallbackSemaphore* sem;
        std::memcpy(&sem, m_storage, sizeof(sem));
        std::unique_lock<std::mutex> lock(sem->mtx);
        ++sem->count;
        sem->cv.notify_one();
    }
}
#endif
