#pragma once
#include <Engine/JobSystem/JobCounter.h>
#include <atomic>

namespace Zero
{
    class JobCounterPool
    {
    public:
        static JobCounterPool& Get();

        JobCounter& Allocate();
        void Free(JobCounter* counter);

    private:
        JobCounterPool() = default;

        // Lock-free freelist
        std::atomic<JobCounter*> m_head{ nullptr };
    };
}
