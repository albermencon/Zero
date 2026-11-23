#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>
#include <Engine/Export.h>
#include <Engine/Ecs/ComponentMask.h>

// Forward declarations — avoids pulling EcsRegistry.h / EcsCommandQueue.h into Scheduler.h
namespace Zero { class EcsRegistry; struct EcsCommandQueue; class BlockingThreadPool; }

namespace Zero
{
    // Explicit frame execution phases.
    // Application::Run() ticks them in order with FlushCommands() boundaries:
    //   PrePhysics -> FixedStep (0..N substeps) -> PostPhysics -> RenderSync
    enum class SchedulePhase : uint8_t
    {
        PrePhysics  = 0,
        FixedStep   = 1,
        PostPhysics = 2,
        RenderSync  = 3,
        Count       = 4
    };

    // Controls where a system executes during DispatchFrame / DispatchPhase.
    enum class ThreadAffinity : uint8_t
    {
        AnyThread  = 0, // Dispatched onto BlockingThreadPool worker threads
        MainThread = 1  // Executed inline on the caller (main) thread
    };

    // Non-component external resource conflict bitmask (e.g., PhysicsWorld, Scene).
    // Two systems conflict if one writes a resource bit that the other reads or writes.
    using ResourceMask = uint64_t;

    namespace ResourceBit
    {
        inline constexpr ResourceMask None         = 0;
        inline constexpr ResourceMask PhysicsWorld = 1ULL << 0;
        inline constexpr ResourceMask Scene        = 1ULL << 1;
        inline constexpr ResourceMask Audio        = 1ULL << 2;
        inline constexpr ResourceMask Input        = 1ULL << 3;
        inline constexpr ResourceMask UserStart    = 1ULL << 16; // custom game resource bits
    }

    struct SystemAccess
    {
        ComponentMask reads          = {};
        ComponentMask writes         = {};
        ResourceMask  resourceReads  = ResourceBit::None;
        ResourceMask  resourceWrites = ResourceBit::None;
    };

    struct SystemDescriptor
    {
        const char*    name     = nullptr;
        SystemAccess   access   = {};
        // std::function allows systems to bind instance state (e.g., RenderSyncSystem,
        // PhysicsSyncSystem). Receives the registry, frame command queue, step dt,
        // and the monotonic registry epoch for this dispatch layer.
        std::function<void(EcsRegistry&, EcsCommandQueue&, float dt, uint64_t epoch)> execute;
        SchedulePhase  phase    = SchedulePhase::PrePhysics;
        ThreadAffinity affinity = ThreadAffinity::AnyThread;
    };

    struct ExecutionLayer
    {
        std::vector<const SystemDescriptor*> systems;
    };

    struct PhaseSchedule
    {
        std::array<std::vector<ExecutionLayer>, static_cast<size_t>(SchedulePhase::Count)> phases;

        [[nodiscard]] const std::vector<ExecutionLayer>& LayersFor(SchedulePhase phase) const noexcept
        {
            return phases[static_cast<size_t>(phase)];
        }
    };

    // Longest-path DAG layer builder — O(N^2) in system count, runs on startup or
    // registration changes. Preserves registration order across any component or
    // external-resource RAW / WAW / WAR dependency edge.
    [[nodiscard]] ZERO_API std::vector<ExecutionLayer> BuildExecutionLayers(
        std::span<const SystemDescriptor> descs);

    // Partitions systems by SchedulePhase and builds conflict-free execution layers
    // per phase. Stored pointers reference elements of `descs` directly.
    [[nodiscard]] ZERO_API PhaseSchedule BuildPhaseSchedule(
        std::span<const SystemDescriptor> descs);

    // Dispatches all layers sequentially. Within each layer, AnyThread systems run
    // in parallel via BlockingThreadPool while MainThread systems execute inline
    // on the calling thread, followed by a layer barrier before the next layer.
    // Advances EcsRegistry's monotonic epoch once per layer.
    ZERO_API void DispatchFrame(const std::vector<ExecutionLayer>& layers,
                                EcsRegistry& reg,
                                EcsCommandQueue& queue,
                                BlockingThreadPool& pool,
                                float dt);

    // Convenience helper to dispatch a single phase from a PhaseSchedule.
    ZERO_API void DispatchPhase(const PhaseSchedule& schedule,
                                SchedulePhase phase,
                                EcsRegistry& reg,
                                EcsCommandQueue& queue,
                                BlockingThreadPool& pool,
                                float dt);
}
