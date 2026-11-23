#include "pch.h"
#include <algorithm>
#include <cstring>
#include <Engine/Ecs/Scheduler.h>
#include <Engine/Ecs/EcsRegistry.h>
#include "JobSystem/BlockingThreadPool.h"
#include <Engine/JobSystem/JobSystem.h>
#include <Engine/JobSystem/JobCounter.h>
#include <Engine/JobSystem/Job.h>

namespace Zero
{
    static bool HasAccessConflict(const SystemAccess& a, const SystemAccess& b) noexcept
    {
        return a.writes.ContainsAny(b.reads)  ||
               a.writes.ContainsAny(b.writes) ||
               a.reads.ContainsAny(b.writes);
    }

    // BuildExecutionLayers
    //
    // DAG longest-path layer assignment preserving registration order on any
    // data-dependency edge (RAW / WAW / WAR).
    // If i < j and HasAccessConflict(descs[i], descs[j]), then layerOf[j] >= layerOf[i] + 1,
    // ensuring a later reader S2 is never scheduled into an earlier layer than a skipped writer S1.
    //
    // O(N^2) in system count — runs only on startup / system registration changes.
    std::vector<ExecutionLayer> BuildExecutionLayers(std::span<const SystemDescriptor> descs)
    {
        if (descs.empty())
            return {};

        std::vector<uint32_t> layerOf(descs.size(), 0);
        uint32_t maxLayer = 0;

        for (size_t j = 0; j < descs.size(); ++j)
        {
            uint32_t layer = 0;
            for (size_t i = 0; i < j; ++i)
            {
                if (HasAccessConflict(descs[i].access, descs[j].access))
                    layer = std::max(layer, layerOf[i] + 1u);
            }
            layerOf[j] = layer;
            maxLayer = std::max(maxLayer, layer);
        }

        std::vector<ExecutionLayer> layers(maxLayer + 1u);
        for (size_t i = 0; i < descs.size(); ++i)
            layers[layerOf[i]].systems.push_back(&descs[i]);

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

        // Persistent scratch buffer reused across all layers and frames — zero hot-path allocations.
        static thread_local std::vector<Job> s_jobs;

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
            s_jobs.clear();
            s_jobs.reserve(layer.systems.size());

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

                s_jobs.push_back(job);
            }

            pool.enqueue_bulk(s_jobs, counter);

            counter.Wait();
            counter.Release();
        }
    }
}
