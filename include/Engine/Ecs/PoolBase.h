#pragma once
#include <cstdint>
#include <vector>
#include <Engine/Core.h>

// Forward declaration — defined in CachedView.h
namespace Zero { struct CachedViewBase; }

namespace Zero
{
    // Type-erased pool base.
    // Function pointers replace virtual dispatch in every hot path.
    // All pointers in this struct are stable for the registry's lifetime —
    // m_pools is a unique_ptr array that never reallocates.
    struct PoolBase
    {
        // Called during FlushCommands() to remove one entity from this pool.
        using DestroyEntityFn = void(*)(PoolBase*, uint32_t entityIdx);
        // Called on scene reset — wipes all data but keeps the pool object alive.
        using ResetFn         = void(*)(PoolBase*);
        // Called during FlushCommands() to insert a component from a type-erased payload.
        // payload points to either EcsCommand::inlineData or a slab address.
        using InsertFn        = void(*)(PoolBase*, uint32_t entityIdx, const void* payload, uint32_t currentFrame);
        // Type-erased dense-array access so views can iterate the smallest pool
        // without a cross-type cast (UB when the smallest pool is not the first
        // type in the view's pack).
        using DenseEntityAtFn = uint32_t(*)(const PoolBase*, uint32_t denseIdx);

#ifdef ECS_VALIDATE
        // Called by ValidateAll() — asserts all 10 invariants for this pool.
        using ValidateFn = void(*)(const PoolBase*);
        ValidateFn validateFn = nullptr;
#endif

        DestroyEntityFn destroyEntityFn = nullptr;
        ResetFn         resetFn         = nullptr;
        InsertFn        insertFn        = nullptr;
        DenseEntityAtFn denseEntityAt   = nullptr;

        uint32_t componentId       = UINT32_MAX;
        uint32_t count             = 0;          // mirrors data.size()
        bool     structurallyDirty = false;      // set in Insert/Remove, cleared post-flush

        // RAII-managed by CachedViewRegistration — never touched directly.
        std::vector<CachedViewBase*> dependentViews;
    };
}
