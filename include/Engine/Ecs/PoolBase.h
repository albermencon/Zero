#pragma once
#include <cstdint>
#include <vector>
#include <Engine/Core.h>
#include <Engine/Ecs/Entity.h>

namespace Zero { struct CachedViewBase; }

namespace Zero
{
    // Provenance of a component write — enables subsystems to distinguish
    // gameplay/kinematic teleports from physics simulation write-backs and
    // prevent pre-step/post-step dirty feedback loops.
    enum class WriteSource : uint8_t
    {
        None     = 0,
        Command  = 1u << 0, // EcsRegistry::FlushCommands (AddComponent / SetComponent)
        Gameplay = 1u << 1, // Gameplay / AI / animation / kinematic teleport writes
        Physics  = 1u << 2, // Physics simulation write-back (FixedStep / PostPhysics)
        System   = 1u << 3, // Other engine system writes
        Any      = 0xFFu
    };

    [[nodiscard]] inline constexpr WriteSource operator|(WriteSource a, WriteSource b) noexcept
    {
        return static_cast<WriteSource>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
    }

    [[nodiscard]] inline constexpr WriteSource operator&(WriteSource a, WriteSource b) noexcept
    {
        return static_cast<WriteSource>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
    }

    [[nodiscard]] inline constexpr bool HasWriteSource(WriteSource value, WriteSource mask) noexcept
    {
        return (static_cast<uint8_t>(value) & static_cast<uint8_t>(mask)) != 0;
    }

    // Type-erased pool base.
    // Function pointers replace virtual dispatch in every hot path.
    // All pointers in this struct are stable for the registry's lifetime —
    // m_pools is a unique_ptr array that never reallocates.
    struct PoolBase
    {
        // Virtual destructor required because EcsRegistry owns ComponentPool<T>
        // instances via std::unique_ptr<PoolBase>. Defined in EcsRegistry.cpp
        // to safely detach any surviving CachedViews on teardown.
        virtual ~PoolBase();

        // Called during FlushCommands() to remove one entity from this pool.
        // Receives the full Entity handle so the pool records an accurate
        // tombstone before swap-and-pop removes the data.
        using DestroyEntityFn   = void(*)(PoolBase*, Entity entity);
        // Called on scene reset — wipes all data and tombstones but keeps the pool alive.
        using ResetFn           = void(*)(PoolBase*);
        // Called during FlushCommands() to insert/upsert a component from a type-erased payload.
        using InsertFn          = void(*)(PoolBase*, uint32_t entityIdx, const void* payload,
                                          uint64_t epoch, WriteSource source);
        // Type-erased dense-array access so views can iterate the smallest pool
        // without a cross-type cast.
        using DenseEntityAtFn   = uint32_t(*)(const PoolBase*, uint32_t denseIdx);
        // Called at end-of-frame by EcsRegistry::ClearTombstones().
        using ClearTombstonesFn = void(*)(PoolBase*);

#ifdef ECS_VALIDATE
        // Called by ValidateAll() — asserts all structural invariants for this pool.
        using ValidateFn = void(*)(const PoolBase*);
        ValidateFn validateFn = nullptr;
#endif

        DestroyEntityFn   destroyEntityFn   = nullptr;
        ResetFn           resetFn           = nullptr;
        InsertFn          insertFn          = nullptr;
        DenseEntityAtFn   denseEntityAt     = nullptr;
        ClearTombstonesFn clearTombstonesFn = nullptr;

        uint32_t componentId       = UINT32_MAX;
        uint32_t count             = 0;          // mirrors data.size()
        bool     structurallyDirty = false;      // set in Insert/Remove, cleared post-flush
        bool     hasTombstones     = false;      // set in Remove, cleared in ClearTombstones()

        // RAII-managed by CachedViewBase — never touched directly.
        std::vector<CachedViewBase*> dependentViews;
    };
}
