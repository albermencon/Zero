#include "pch.h"
#include "JobCounterPool.h"

namespace Zero
{
    JobCounterPool& JobCounterPool::Get()
    {
        static JobCounterPool instance;
        return instance;
    }

    JobCounter& JobCounterPool::Allocate()
    {
        // Pop from freelist
        JobCounter* head = m_head.load(std::memory_order_acquire);
        while (head)
        {
            if (m_head.compare_exchange_weak(head, head->m_next,
                std::memory_order_acq_rel, std::memory_order_acquire))
            {
                // Recycle — reset state before handing out
                head->pending.store(0, std::memory_order_relaxed);
                // semaphore must be at 0 — it was drained by the last Wait()
                head->m_next = nullptr;
                return *head;
            }
        }

        // Pool empty — heap allocate
        return *(new JobCounter{});
    }

    void JobCounterPool::Free(JobCounter* counter)
    {
        // Push onto freelist
        counter->m_next = m_head.load(std::memory_order_relaxed);
        while (!m_head.compare_exchange_weak(counter->m_next, counter,
            std::memory_order_acq_rel, std::memory_order_relaxed));
    }
}
