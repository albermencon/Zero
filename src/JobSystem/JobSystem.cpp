#include "pch.h"
#include <Engine/JobSystem/JobSystem.h>
#include "JobSystem/BlockingThreadPool.h"
#include <span>

namespace Zero
{
    void Enqueue(Job job)
    {
        BlockingThreadPool::Get().enqueue(std::move(job));
    }

    void Enqueue(Job job, JobCounter& counter)
    {
        BlockingThreadPool::Get().enqueue(std::move(job), counter);
    }

    void EnqueueBulk(std::span<const Job> jobs)
    {
        BlockingThreadPool::Get().enqueue_bulk(jobs);
    }

    void EnqueueBulk(std::span<const Job> jobs, JobCounter& counter)
    {
        BlockingThreadPool::Get().enqueue_bulk(jobs, counter);
    }

    JobCounter& MakeCounter()
    {
        return BlockingThreadPool::Get().make_counter();
    }

    void WaitIdle()
    {
        BlockingThreadPool::Get().wait_idle();
    }
}
