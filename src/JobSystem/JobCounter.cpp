#include "pch.h"
#include <Engine/JobSystem/JobCounter.h>
#include "JobCounterPool.h"

namespace Zero
{
    void JobCounter::Wait()
    {
        semaphore.acquire();
    }

    void JobCounter::Release()
    {
        JobCounterPool::Get().Free(this);
    }

    void JobCounter::Decrement()
    {
        if (pending.fetch_sub(1, std::memory_order_acq_rel) == 1)
                semaphore.release(); // last job — wake waiter
    }
}
