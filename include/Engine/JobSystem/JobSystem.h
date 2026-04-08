#pragma once
#include <Engine/Core.h>
#include "Job.h"
#include "JobCounter.h"
#include <span>
#include <cstdint>

namespace Zero
{
    template<typename TData>
    [[nodiscard]] ENGINE_API Job make_job(void(*fn)(void*), const TData& data)
    {
        static_assert(sizeof(TData) <= sizeof(Job::payload),
            "Data too large for inline payload — use make_job_external().");
        static_assert(std::is_trivially_copyable_v<TData>,
            "Inline job data must be trivially copyable.");

        Job job;
        job.fn   = fn;
        job.mode = Job::Mode::Inline;
        std::memcpy(job.payload, &data, sizeof(TData));
        return job;
    }

    // External pointer — caller owns and guarantees lifetime
    [[nodiscard]] ENGINE_API inline Job make_job_external(void(*fn)(void*), void* ptr)
    {
        Job job;
        job.fn   = fn;
        job.ptr  = ptr;
        job.mode = Job::Mode::External;
        return job;
    }

    // Fire-and-forget
    ENGINE_API void Enqueue(Job job);

    // Tracked — counter.pending is pre-incremented before the job hits the queue
    ENGINE_API void Enqueue(Job job, JobCounter& counter);

    //  Bulk enqueue

    ENGINE_API void EnqueueBulk(std::span<const Job> jobs);
    ENGINE_API void EnqueueBulk(std::span<const Job> jobs, JobCounter& counter);

    // Sync + Counter

    // Allocate from pool — always pair with counter.Release() after Wait()
    [[nodiscard]] ENGINE_API JobCounter& MakeCounter();

    void ENGINE_API WaitIdle();
}

// Fire-and-forget
#define ENQUEUE_LAMBDA(callable)            ::Zero::Enqueue(::Zero::make_job(callable))
#define ENQUEUE_JOB(fn, data)               ::Zero::Enqueue(::Zero::make_job((fn), (data)))
#define ENQUEUE_JOB_EXT(fn, ptr)            ::Zero::Enqueue(::Zero::make_job_external((fn), (ptr)))

// Tracked
#define ENQUEUE_JOB_C(counter, fn, data)    ::Zero::Enqueue(::Zero::make_job((fn), (data)), (counter))
#define ENQUEUE_JOB_EXT_C(counter, fn, ptr) ::Zero::Enqueue(::Zero::make_job_external((fn), (ptr)), (counter))

// Bulk
#define ENQUEUE_BULK(jobs_span)             ::Zero::EnqueueBulk((jobs_span))
#define ENQUEUE_BULK_C(counter, jobs_span)  ::Zero::EnqueueBulk((jobs_span), (counter))
