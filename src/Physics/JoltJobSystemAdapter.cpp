#include "pch.h"
#include "Physics/JoltJobSystemAdapter.h"
#include <Engine/JobSystem/JobSystem.h>
#include <algorithm>
#include <chrono>
#include <thread>

namespace Zero
{
    JoltJobSystemAdapter::JoltJobSystemAdapter(BlockingThreadPool& pool) noexcept
        : m_pool(pool)
    {
    }

    JoltJobSystemAdapter::JoltJobSystemAdapter(BlockingThreadPool& pool,
                                               uint32_t maxJobs,
                                               uint32_t maxBarriers)
        : m_pool(pool)
    {
        Init(maxJobs, maxBarriers);
    }

    JoltJobSystemAdapter::~JoltJobSystemAdapter()
    {
        while (m_inFlightQueuedJobs.load(std::memory_order_acquire) > 0)
            std::this_thread::yield();
    }

    void JoltJobSystemAdapter::Init(uint32_t maxJobs, uint32_t maxBarriers)
    {
        if (m_initialized)
            return;

        JobSystemWithBarrier::Init(maxBarriers);
        m_jobs.Init(maxJobs, maxJobs);
        m_initialized = true;
    }

    int JoltJobSystemAdapter::GetMaxConcurrency() const
    {
        if (!m_pool.IsRunning() || !m_pool.IsAcceptingJobs() || m_pool.GetThreadCount() == 0)
            return 1;

        return static_cast<int>(m_pool.GetThreadCount()) + 1;
    }

    JPH::JobHandle JoltJobSystemAdapter::CreateJob(const char* inName,
                                                   JPH::ColorArg inColor,
                                                   const JobFunction& inJobFunction,
                                                   JPH::uint32 inNumDependencies)
    {
        JPH::uint32 index = JPH::FixedSizeFreeList<Job>::cInvalidObjectIndex;
        for (;;)
        {
            index = m_jobs.ConstructObject(inName, inColor, this, inJobFunction, inNumDependencies);
            if (index != JPH::FixedSizeFreeList<Job>::cInvalidObjectIndex)
                break;

            JPH_ASSERT(false, "JoltJobSystemAdapter: no job slots available!");
            std::this_thread::yield();
        }

        Job* job = &m_jobs.Get(index);
        JobHandle handle(job);

        if (inNumDependencies == 0)
            QueueJob(job);

        return handle;
    }

    void JoltJobSystemAdapter::RunJoltJob(void* rawJob) noexcept
    {
        auto* job     = static_cast<Job*>(rawJob);
        auto* adapter = static_cast<JoltJobSystemAdapter*>(job->GetJobSystem());
        job->Execute();
        job->Release();
        adapter->m_inFlightQueuedJobs.fetch_sub(1, std::memory_order_release);
    }

    void JoltJobSystemAdapter::QueueJob(Job* inJob)
    {
        if (!m_pool.IsRunning() || !m_pool.IsAcceptingJobs() || m_pool.GetThreadCount() == 0)
        {
            inJob->AddRef();
            inJob->Execute();
            inJob->Release();
            return;
        }

        m_inFlightQueuedJobs.fetch_add(1, std::memory_order_relaxed);
        inJob->AddRef();
        m_pool.enqueue(make_job_external(&RunJoltJob, inJob));
    }

    void JoltJobSystemAdapter::QueueJobs(Job** inJobs, JPH::uint inNumJobs)
    {
        if (inNumJobs == 0)
            return;

        if (!m_pool.IsRunning() || !m_pool.IsAcceptingJobs() || m_pool.GetThreadCount() == 0)
        {
            for (JPH::uint i = 0; i < inNumJobs; ++i)
            {
                inJobs[i]->AddRef();
                inJobs[i]->Execute();
                inJobs[i]->Release();
            }
            return;
        }

        constexpr size_t kBatchSize = 64;
        Zero::Job batch[kBatchSize];

        m_inFlightQueuedJobs.fetch_add(static_cast<uint32_t>(inNumJobs), std::memory_order_relaxed);

        size_t offset    = 0;
        size_t remaining = static_cast<size_t>(inNumJobs);
        while (remaining > 0)
        {
            const size_t count = std::min(remaining, kBatchSize);
            for (size_t i = 0; i < count; ++i)
            {
                Job* j = inJobs[offset + i];
                j->AddRef();
                batch[i] = make_job_external(&RunJoltJob, j);
            }
            m_pool.enqueue_bulk(std::span<const Zero::Job>(batch, count));
            offset    += count;
            remaining -= count;
        }
    }

    void JoltJobSystemAdapter::FreeJob(Job* inJob)
    {
        m_jobs.DestructObject(inJob);
    }
}
