#include "pch.h"
#include <algorithm>
#include <cstring>
#include <Engine/Ecs/Scheduler.h>
#include <Engine/Ecs/EcsRegistry.h>
#include <Engine/Ecs/EcsCommandQueue.h>
#include "JobSystem/BlockingThreadPool.h"
#include <Engine/JobSystem/JobSystem.h>
#include <Engine/JobSystem/JobCounter.h>
#include <Engine/JobSystem/Job.h>

namespace Zero
{
    static bool HasAccessConflict(const SystemAccess& a, const SystemAccess& b) noexcept
    {
        const bool componentConflict =
            a.writes.ContainsAny(b.reads)  ||
            a.writes.ContainsAny(b.writes) ||
            a.reads.ContainsAny(b.writes);

        const bool resourceConflict =
            ((a.resourceWrites & b.resourceReads)  != 0) ||
            ((a.resourceWrites & b.resourceWrites) != 0) ||
            ((a.resourceReads  & b.resourceWrites) != 0);

        return componentConflict || resourceConflict;
    }

    // DAG longest-path layer assignment preserving registration order across any
    // component or external-resource RAW / WAW / WAR dependency edge.
    static std::vector<ExecutionLayer> BuildLayersFromPointers(
        std::span<const SystemDescriptor* const> ptrs)
    {
        if (ptrs.empty())
            return {};

        std::vector<uint32_t> layerOf(ptrs.size(), 0);
        uint32_t maxLayer = 0;

        for (size_t j = 0; j < ptrs.size(); ++j)
        {
            uint32_t layer = 0;
            for (size_t i = 0; i < j; ++i)
            {
                if (HasAccessConflict(ptrs[i]->access, ptrs[j]->access))
                    layer = std::max(layer, layerOf[i] + 1u);
            }
            layerOf[j] = layer;
            maxLayer   = std::max(maxLayer, layer);
        }

        std::vector<ExecutionLayer> layers(maxLayer + 1u);
        for (size_t i = 0; i < ptrs.size(); ++i)
            layers[layerOf[i]].systems.push_back(ptrs[i]);

        return layers;
    }

    std::vector<ExecutionLayer> BuildExecutionLayers(std::span<const SystemDescriptor> descs)
    {
        if (descs.empty())
            return {};

        std::vector<const SystemDescriptor*> ptrs;
        ptrs.reserve(descs.size());
        for (const auto& d : descs)
            ptrs.push_back(&d);

        return BuildLayersFromPointers(ptrs);
    }

    PhaseSchedule BuildPhaseSchedule(std::span<const SystemDescriptor> descs)
    {
        PhaseSchedule schedule;
        if (descs.empty())
            return schedule;

        constexpr size_t kPhaseCount = static_cast<size_t>(SchedulePhase::Count);
        std::array<std::vector<const SystemDescriptor*>, kPhaseCount> buckets;

        for (const auto& d : descs)
        {
            const size_t p = static_cast<size_t>(d.phase);
            ZERO_CORE_ASSERT(p < kPhaseCount, "BuildPhaseSchedule: invalid SchedulePhase");
            if (p < kPhaseCount)
                buckets[p].push_back(&d);
        }

        for (size_t p = 0; p < kPhaseCount; ++p)
        {
            schedule.phases[p] = BuildLayersFromPointers(buckets[p]);
        }

        return schedule;
    }

    // DispatchFrame / DispatchPhase
    //
    // Flat loop over pre-built layers.
    struct FrameContext
    {
        EcsRegistry*     reg;
        EcsCommandQueue* queue;
        float            dt;
        uint64_t         epoch;
    };

    struct JobCapture
    {
        const SystemDescriptor* sys;
        const FrameContext*     ctx;
    };
    static_assert(sizeof(JobCapture) <= 16, "JobCapture must fit inside 16-byte Job inline payload");

    void DispatchFrame(const std::vector<ExecutionLayer>& layers,
                       EcsRegistry& reg,
                       EcsCommandQueue& queue,
                       BlockingThreadPool& pool,
                       float dt)
    {
        // Persistent scratch buffers reused across all layers and frames — zero hot-path allocations.
        static thread_local std::vector<Job> s_jobs;
        static thread_local std::vector<const SystemDescriptor*> s_mainSystems;

        for (const auto& layer : layers)
        {
            if (layer.systems.empty()) continue;

            const uint64_t layerEpoch = reg.AdvanceEpoch();
            const FrameContext ctx{ &reg, &queue, dt, layerEpoch };

            s_jobs.clear();
            s_mainSystems.clear();

            for (const SystemDescriptor* sys : layer.systems)
            {
                if (!sys || !sys->execute) continue;

                if (sys->affinity == ThreadAffinity::MainThread)
                {
                    s_mainSystems.push_back(sys);
                }
                else
                {
                    Job job{};
                    job.SetMode(Job::Mode::Inline);

                    const JobCapture cap{ sys, &ctx };
                    std::memcpy(job.payload, &cap, sizeof(cap));

                    job.fn = [](void* raw)
                    {
                        JobCapture c;
                        std::memcpy(&c, raw, sizeof(c));
                        c.sys->execute(*c.ctx->reg, *c.ctx->queue, c.ctx->dt, c.ctx->epoch);
                    };

                    s_jobs.push_back(job);
                }
            }

            JobCounter* counter = nullptr;
            if (!s_jobs.empty())
            {
                counter = &pool.make_counter();
                pool.enqueue_bulk(s_jobs, *counter);
            }

            // Execute MainThread systems inline while worker jobs run concurrently
            // (all systems within a layer are conflict-free).
            for (const SystemDescriptor* sys : s_mainSystems)
            {
                sys->execute(reg, queue, dt, layerEpoch);
            }

            if (counter)
            {
                counter->Wait();
                counter->Release();
            }
        }
    }

    void DispatchPhase(const PhaseSchedule& schedule,
                       SchedulePhase phase,
                       EcsRegistry& reg,
                       EcsCommandQueue& queue,
                       BlockingThreadPool& pool,
                       float dt)
    {
        const size_t p = static_cast<size_t>(phase);
        ZERO_CORE_ASSERT(p < static_cast<size_t>(SchedulePhase::Count),
            "DispatchPhase: invalid SchedulePhase");
        if (p >= static_cast<size_t>(SchedulePhase::Count))
            return;

        DispatchFrame(schedule.phases[p], reg, queue, pool, dt);
    }
}
