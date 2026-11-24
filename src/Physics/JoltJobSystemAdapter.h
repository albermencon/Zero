#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Core/JobSystemWithBarrier.h>
#include <Jolt/Core/FixedSizeFreeList.h>

#include "JobSystem/BlockingThreadPool.h"
#include <atomic>
#include <cstdint>

namespace Zero
{
    // Bridges JPH::JobSystemWithBarrier directly onto Zero's existing
    // BlockingThreadPool so Jolt does not spawn redundant worker threads.
    // All Job objects come from a pre-allocated FixedSizeFreeList (zero hot-path heap allocations).
    class JoltJobSystemAdapter final : public JPH::JobSystemWithBarrier
    {
    public:
        JPH_OVERRIDE_NEW_DELETE

        explicit JoltJobSystemAdapter(BlockingThreadPool& pool) noexcept;
        JoltJobSystemAdapter(BlockingThreadPool& pool, uint32_t maxJobs, uint32_t maxBarriers);
        ~JoltJobSystemAdapter() override;

        void Init(uint32_t maxJobs, uint32_t maxBarriers);

        // JPH::JobSystem interface
        int       GetMaxConcurrency() const override;
        JobHandle CreateJob(const char* inName,
                            JPH::ColorArg inColor,
                            const JobFunction& inJobFunction,
                            JPH::uint32 inNumDependencies = 0) override;

    protected:
        void QueueJob(Job* inJob) override;
        void QueueJobs(Job** inJobs, JPH::uint inNumJobs) override;
        void FreeJob(Job* inJob) override;

    private:
        static void RunJoltJob(void* rawJob) noexcept;

        BlockingThreadPool&             m_pool;
        JPH::FixedSizeFreeList<Job>     m_jobs;
        std::atomic<uint32_t>           m_inFlightQueuedJobs{ 0 };
        bool                            m_initialized = false;
    };
}
