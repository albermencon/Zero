#include "pch.h"
#include <Engine/Ecs/Scheduler.h>
#include <Engine/Ecs/EcsRegistry.h>
#include "JobSystem/BlockingThreadPool.h"
#include <Engine/JobSystem/JobSystem.h>
#include <Engine/JobSystem/JobCounter.h>
#include <Engine/JobSystem/Job.h>

namespace Zero
{
    // BuildExecutionLayers
    //
    // Greedy layer assignment.
    // A system can join the current layer if its read/write mask doesn't conflict
    // with any write mask already committed to that layer.
    // Conflict definition:
    //   layerWrites.ContainsAny(system.reads)  — someone writes what we read
    //   layerWrites.ContainsAny(system.writes) — two writers to same component
    //   system.writes.ContainsAny(layerWrites) — same, commutative check
    //
    // O(N^2) in system count — acceptable for N < 64, runs once at startup.
    std::vector<ExecutionLayer> BuildExecutionLayers(std::span<const SystemDescriptor> descs)
    {
        std::vector<ExecutionLayer> layers;
        std::vector<bool> assigned(descs.size(), false);

        while (true)
        {
            ExecutionLayer layer;
            ComponentMask  layerWrites;
            ComponentMask  layerReads;

            for (size_t i = 0; i < descs.size(); ++i)
            {
                if (assigned[i]) continue;
                const auto& d = descs[i];

                const bool conflict =
                    layerWrites.ContainsAny(d.access.reads)   ||
                    layerWrites.ContainsAny(d.access.writes)  ||
                    layerReads.ContainsAny(d.access.writes);

                if (!conflict)
                {
                    layer.systems.push_back(&d);
                    layerWrites.lo |= d.access.writes.lo;
                    layerWrites.hi |= d.access.writes.hi;
                    layerReads.lo  |= d.access.reads.lo;
                    layerReads.hi  |= d.access.reads.hi;
                    assigned[i] = true;
                }
            }

            if (layer.systems.empty()) break;
            layers.push_back(std::move(layer));
        }

        return layers;
    }

    // DispatchFrame
    //
    // Flat loop over pre-built layers.
    // Per layer: enqueue all systems as jobs, wait on counter, release counter.
    struct FrameContext
    {
        EcsRegistry* reg;
        float dt;
        uint32_t currentFrame;
    };

    struct JobCapture
    {
        const SystemDescriptor* sys;
        const FrameContext* ctx;
    };
    static_assert(sizeof(JobCapture) <= 16, "JobCapture must fit inside 16-byte Job inline payload");

    void DispatchFrame(const std::vector<ExecutionLayer>& layers,
                       EcsRegistry& reg,
                       BlockingThreadPool& pool,
                       float dt,
                       uint32_t currentFrame)
    {
        const FrameContext ctx{ &reg, dt, currentFrame };

        for (const auto& layer : layers)
        {
            if (layer.systems.empty()) continue;

            JobCounter& counter = pool.make_counter();

            // Build all jobs first, then bulk-enqueue. enqueue_bulk increments
            // counter.pending by the TOTAL job count before any job becomes
            // visible to workers. With per-job enqueue(), a fast worker could
            // complete job N-1 (pending 1->0, semaphore released) while the
            // producer was still enqueuing job N — Wait() then returned early
            // and the next layer raced the still-running job.
            std::vector<Job> jobs;
            jobs.reserve(layer.systems.size());

            for (const SystemDescriptor* sys : layer.systems)
            {
                Job job{};
                job.SetMode(Job::Mode::Inline);

                const JobCapture cap{ sys, &ctx };
                std::memcpy(job.payload, &cap, sizeof(cap));

                job.fn = [](void* raw)
                {
                    JobCapture c;
                    std::memcpy(&c, raw, sizeof(c));
                    c.sys->execute(*c.ctx->reg, c.ctx->dt, c.ctx->currentFrame);
                };

                jobs.push_back(job);
            }

            pool.enqueue_bulk(jobs, counter);

            counter.Wait();
            counter.Release();
        }
    }
}
