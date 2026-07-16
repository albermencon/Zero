#include <doctest.h>

#include <Engine/Thread/Thread.h>
#include <Engine/Thread/Mutex.h>
#include <Engine/Thread/RecursiveMutex.h>
#include <Engine/Thread/ScopedLock.h>
#include <Engine/Thread/ConditionVariable.h>
#include <Engine/Thread/Semaphore.h>

#include <vector>
#include <atomic>
#include <tuple>
#include <utility>

using namespace Zero;

// Helper bounded queue for multi-producer multi-consumer CV testing
template <typename T, size_t Capacity>
class BoundedQueue
{
public:
    void Push(const T& val)
    {
        m_mutex.Lock();
        while (m_queue.size() >= Capacity)
        {
            m_notFull.Wait(m_mutex);
        }
        m_queue.push_back(val);
        m_mutex.Unlock();
        m_notEmpty.NotifyOne();
    }

    T Pop()
    {
        m_mutex.Lock();
        while (m_queue.empty())
        {
            m_notEmpty.Wait(m_mutex);
        }
        T val = m_queue.front();
        m_queue.erase(m_queue.begin());
        m_mutex.Unlock();
        m_notFull.NotifyOne();
        return val;
    }

private:
    Mutex m_mutex;
    ConditionVariable m_notFull;
    ConditionVariable m_notEmpty;
    std::vector<T> m_queue;
};

TEST_CASE("ThreadPrimitives: Mutex lock/unlock Contention")
{
    Mutex mtx;
    int counter = 0;
    
    auto func = [](Mutex* m, int* c) {
        for (int i = 0; i < 10000; ++i)
        {
            ScopedLock<Mutex> lock(*m);
            (*c)++;
        }
    };

    std::vector<Thread> threads;
    threads.reserve(20);
    for (int i = 0; i < 20; ++i)
    {
        threads.emplace_back(func, &mtx, &counter);
    }

    for (auto& t : threads)
        t.Join();

    CHECK(counter == 200000);
}

TEST_CASE("ThreadPrimitives: Mutex TryLock behavior")
{
    Mutex mtx;
    std::atomic<bool> lockAcquired{false};
    std::atomic<bool> threadReady{false};

    mtx.Lock();

    auto worker = [](Mutex* m, std::atomic<bool>* acquired, std::atomic<bool>* ready) {
        *ready = true;
        *acquired = m->TryLock();
        if (*acquired)
        {
            m->Unlock();
        }
    };

    Thread t(worker, &mtx, &lockAcquired, &threadReady);
    while (!threadReady.load())
    {
        Thread::Yield();
    }
    t.Join();

    CHECK(lockAcquired.load() == false);

    mtx.Unlock();
    CHECK(mtx.TryLock() == true);
    mtx.Unlock();
}

TEST_CASE("ThreadPrimitives: RecursiveMutex Nesting and Contention")
{
    RecursiveMutex mtx;
    int counter = 0;
    
    auto func = [](RecursiveMutex* m, int* c) {
        for (int i = 0; i < 5000; ++i)
        {
            ScopedLock<RecursiveMutex> lock1(*m);
            {
                ScopedLock<RecursiveMutex> lock2(*m);
                {
                    ScopedLock<RecursiveMutex> lock3(*m);
                    (*c)++;
                }
            }
        }
    };

    std::vector<Thread> threads;
    threads.reserve(10);
    for (int i = 0; i < 10; ++i)
    {
        threads.emplace_back(func, &mtx, &counter);
    }

    for (auto& t : threads)
        t.Join();

    CHECK(counter == 50000);
}

TEST_CASE("ThreadPrimitives: RecursiveMutex TryLock behavior")
{
    RecursiveMutex mtx;
    CHECK(mtx.TryLock() == true);
    CHECK(mtx.TryLock() == true); // Recursive lock via TryLock
    mtx.Unlock();
    mtx.Unlock();
}

TEST_CASE("ThreadPrimitives: ConditionVariable Multi-Producer Multi-Consumer")
{
    BoundedQueue<int, 10> queue;
    std::atomic<int> sumPushed{0};
    std::atomic<int> sumPopped{0};
    const int itemsPerProducer = 1000;
    const int numProducers = 4;
    const int numConsumers = 4;

    auto producer = [](BoundedQueue<int, 10>* q, std::atomic<int>* total, int count) {
        for (int i = 0; i < count; ++i)
        {
            q->Push(1);
            total->fetch_add(1, std::memory_order_relaxed);
        }
    };

    auto consumer = [](BoundedQueue<int, 10>* q, std::atomic<int>* total, int count) {
        for (int i = 0; i < count; ++i)
        {
            int val = q->Pop();
            total->fetch_add(val, std::memory_order_relaxed);
        }
    };

    std::vector<Thread> producers;
    std::vector<Thread> consumers;
    
    producers.reserve(numProducers);
    consumers.reserve(numConsumers);

    for (int i = 0; i < numProducers; ++i)
    {
        producers.emplace_back(producer, &queue, &sumPushed, itemsPerProducer);
    }

    for (int i = 0; i < numConsumers; ++i)
    {
        consumers.emplace_back(consumer, &queue, &sumPopped, itemsPerProducer);
    }

    for (auto& t : producers) t.Join();
    for (auto& t : consumers) t.Join();

    CHECK(sumPushed.load() == numProducers * itemsPerProducer);
    CHECK(sumPopped.load() == numProducers * itemsPerProducer);
}

TEST_CASE("ThreadPrimitives: Semaphore Backpressure Limiter")
{
    Semaphore sem(3);
    std::atomic<int> activeThreads{0};
    std::atomic<int> maxSeenActive{0};
    std::atomic<int> totalProcessed{0};

    auto worker = [](Semaphore* s, std::atomic<int>* active, std::atomic<int>* maxSeen, std::atomic<int>* total) {
        for (int i = 0; i < 50; ++i)
        {
            s->Wait();
            int current = active->fetch_add(1, std::memory_order_relaxed) + 1;
            
            // Track the maximum concurrent threads inside this block
            int expectedMax = maxSeen->load(std::memory_order_relaxed);
            while (current > expectedMax && !maxSeen->compare_exchange_weak(expectedMax, current, std::memory_order_relaxed)) {}

            Thread::Sleep(1);

            active->fetch_sub(1, std::memory_order_relaxed);
            s->Signal();
            total->fetch_add(1, std::memory_order_relaxed);
        }
    };

    std::vector<Thread> threads;
    threads.reserve(10);
    for (int i = 0; i < 10; ++i)
    {
        threads.emplace_back(worker, &sem, &activeThreads, &maxSeenActive, &totalProcessed);
    }

    for (auto& t : threads)
        t.Join();

    CHECK(maxSeenActive.load() <= 3);
    CHECK(totalProcessed.load() == 500);
}

TEST_CASE("ThreadPrimitives: Deadlock Prevention / Lock Ordering")
{
    Mutex mtxA;
    Mutex mtxB;
    std::atomic<int> counter{0};

    auto worker = [](Mutex* first, Mutex* second, std::atomic<int>* c) {
        for (int i = 0; i < 5000; ++i)
        {
            first->Lock();
            second->Lock();
            (*c)++;
            second->Unlock();
            first->Unlock();
        }
    };

    Thread t1(worker, &mtxA, &mtxB, &counter);
    Thread t2(worker, &mtxA, &mtxB, &counter);

    t1.Join();
    t2.Join();

    CHECK(counter.load() == 10000);
}

#if defined(ZERO_ENABLE_PROFILING)
#include <tracy/Tracy.hpp>
#include <mutex>

TEST_CASE("ThreadPrimitives: TracyLockable integration")
{
    TracyLockable(Mutex, tracyMtx);
    TracyLockable(RecursiveMutex, tracyRecMtx);

    {
        std::lock_guard<LockableBase(Mutex)> lock(tracyMtx);
    }
    {
        std::lock_guard<LockableBase(RecursiveMutex)> lock(tracyRecMtx);
    }
}
#endif

