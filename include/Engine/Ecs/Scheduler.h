#pragma once
#include <cstdint>
#include <vector>
#include <span>
#include <functional>
#include <Engine/Ecs/ComponentMask.h>

// Forward declarations — avoids pulling EcsRegistry.h into Scheduler.h
namespace Zero { class EcsRegistry; class BlockingThreadPool; }

namespace Zero
{
    // Scheduler
    //
    // Compile-time-resolved system access masks → static execution layer graph.
    // BuildExecutionLayers runs once at startup (O(N^2) in system count, N < 64).
    // DispatchFrame is a flat loop — no DAG traversal per frame.

    struct SystemAccess
    {
        ComponentMask reads;
        ComponentMask writes;
    };

    struct SystemDescriptor
    {
        const char*  name;
        SystemAccess access;
        // std::function allows systems to bind instance state (e.g. a
        // RenderSyncSystem) instead of being forced into stateless callbacks.
        std::function<void(EcsRegistry&, float dt, uint32_t currentFrame)> execute;
    };

    struct ExecutionLayer
    {
        std::vector<const SystemDescriptor*> systems;
    };

    // Greedy layer builder — O(N^2) in system count, runs once at startup.
    // Returns layers such that no two systems in the same layer have conflicting
    // read/write access. Systems execute in parallel within a layer; layers
    // are serialised (barrier between each layer).
    std::vector<ExecutionLayer> BuildExecutionLayers(std::span<const SystemDescriptor> descs);

    // Dispatch all layers sequentially. Each layer runs all its systems in parallel
    // via BlockingThreadPool, then waits on the counter before starting the next.
    void DispatchFrame(const std::vector<ExecutionLayer>& layers,
                       EcsRegistry& reg,
                       BlockingThreadPool& pool,
                       float dt,
                       uint32_t currentFrame);
}
