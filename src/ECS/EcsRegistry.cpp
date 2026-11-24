#include "pch.h"
#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <Engine/Ecs/EcsRegistry.h>
#include <Engine/Ecs/CachedView.h>
#include "EntityAllocator.h"

namespace Zero
{
    // Static-init duplicate component ID guard (cross-TU, no RTTI)
    namespace Detail
    {
        const void* RegisterComponentIdToken(uint32_t id,
                                             const void* typeToken,
                                             const char* typeName) noexcept
        {
            static const void* s_tokens[EcsRegistry::MaxComponents]{};
            static const char* s_names[EcsRegistry::MaxComponents]{};

            if (id >= EcsRegistry::MaxComponents)
            {
                std::fprintf(stderr,
                    "ECS Error: Component '%s' registered with out-of-range ID %u\n",
                    typeName ? typeName : "<unknown>", id);
                DEBUGBREAK();
                std::abort();
            }

            if (s_tokens[id] != nullptr && s_tokens[id] != typeToken)
            {
                std::fprintf(stderr,
                    "ECS Error: Duplicate Component ID %u claimed by '%s' (already claimed by '%s')\n",
                    id,
                    typeName ? typeName : "<unknown>",
                    s_names[id] ? s_names[id] : "<unknown>");
                DEBUGBREAK();
                std::abort();
            }

            s_tokens[id] = typeToken;
            s_names[id]  = typeName;
            return typeToken;
        }
    }

    // PoolBase destructor — detaches surviving CachedViews to prevent UAF if
    // EcsRegistry is destroyed before a CachedView.
    PoolBase::~PoolBase()
    {
        for (CachedViewBase* view : dependentViews)
        {
            if (!view) continue;
            auto& vp = view->pools;
            vp.erase(std::remove(vp.begin(), vp.end(), this), vp.end());
            if (view->primary == this)
                view->primary = nullptr;
        }
        dependentViews.clear();
    }

    // EcsRegistry — constructor / destructor
    EcsRegistry::EcsRegistry()
        : m_allocator(std::make_unique<EntityAllocator>())
        , m_componentMask(std::make_unique<ComponentMask[]>(MaxEntities))
    {
        // Resize once — all unique_ptrs default to nullptr.
        // Never reallocated: all PoolBase* references handed to views remain stable.
        m_pools.resize(MaxComponents);
        m_destroyedEntities.reserve(256);
    }

    // unique_ptr members require a destructor definition here because EntityAllocator
    // is an incomplete type in the public header.
    EcsRegistry::~EcsRegistry() = default;

    // Entity lifecycle
    Entity EcsRegistry::CreateEntity() noexcept
    {
        return m_allocator->MintEntity();
    }

    void EcsRegistry::DestroyEntity(Entity e, EcsCommandQueue& q)
    {
        ZERO_CORE_ASSERT(m_allocator->IsAlive(e), "DestroyEntity called on dead entity");
        q.EnqueueDestroy(e);
    }

    bool EcsRegistry::IsAlive(Entity e) const noexcept
    {
        return m_allocator->IsAlive(e);
    }

    Entity EcsRegistry::CurrentGeneration(uint32_t entityIdx) const noexcept
    {
        const uint32_t gen = m_allocator->CurrentGeneration(entityIdx);
        return Make(entityIdx, gen);
    }

    // Mask access
    const ComponentMask& EcsRegistry::Mask(uint32_t entityIdx) const noexcept
    {
        ZERO_ASSERT(entityIdx < MaxEntities, "entityIdx out of range");
        if (entityIdx >= MaxEntities)
        {
            static const ComponentMask s_emptyMask{};
            return s_emptyMask;
        }
        return m_componentMask[entityIdx];
    }

    // Static file-local helpers
    static void InvalidateViews(PoolBase* base) noexcept
    {
        for (CachedViewBase* v : base->dependentViews)
        {
            if (v) v->Invalidate();
        }
    }

    static void FreeHeapPayload(const EcsCommand& cmd) noexcept
    {
        if ((cmd.flags & kEcsFlagHeapPayload) && cmd.heapPtr)
        {
            ::operator delete(const_cast<void*>(cmd.heapPtr),
                              std::align_val_t{ cmd.heapAlign });
        }
    }

    // DispatchAdd
    void EcsRegistry::DispatchAdd(const EcsCommand& cmd, EcsCommandQueue& q, uint64_t epoch)
    {
        // Generation check: a stale handle (entity destroyed and its slot
        // recycled since the command was enqueued) must be dropped, not applied
        // to the recycled slot. Always free any heap-fallback payload first.
        if (!m_allocator->IsAlive(cmd.entity))
        {
            FreeHeapPayload(cmd);
            return;
        }

        const uint32_t entityIdx = Index(cmd.entity);
        ZERO_CORE_ASSERT(entityIdx > 0 && entityIdx < MaxEntities,
            "DispatchAdd: invalid entity index");
        if (entityIdx == 0 || entityIdx >= MaxEntities)
        {
            FreeHeapPayload(cmd);
            return;
        }

        const uint8_t cid = cmd.componentId;
        ZERO_ASSERT(cid < MaxComponents, "DispatchAdd: componentId out of range");
        PoolBase* pool = (cid < MaxComponents) ? m_poolPtr[cid].load(std::memory_order_acquire) : nullptr;
        ZERO_ASSERT(pool != nullptr,
            "DispatchAdd: pool does not exist — GetOrCreatePool<T> must be called first");
        if (!pool)
        {
            FreeHeapPayload(cmd);
            return;
        }

        const void* payload = nullptr;
        if (cmd.flags & kEcsFlagSlabPayload)
            payload = static_cast<const void*>(q.slab.Data(cmd.slabOffset, cmd.slabSize));
        else if (cmd.flags & kEcsFlagHeapPayload)
            payload = cmd.heapPtr;
        else
            payload = static_cast<const void*>(cmd.inlineData);

        if (!payload)
        {
            FreeHeapPayload(cmd);
            return;
        }

        pool->insertFn(pool, entityIdx, payload, epoch, WriteSource::Command);
        m_componentMask[entityIdx].Set(cid);

        // Free heap-fallback payloads after they have been copied into the pool.
        FreeHeapPayload(cmd);
    }

    // DispatchRemove
    void EcsRegistry::DispatchRemove(const EcsCommand& cmd)
    {
        // Generation check — never touch a recycled slot for a dead handle.
        if (!m_allocator->IsAlive(cmd.entity)) return;

        const uint32_t entityIdx = Index(cmd.entity);
        ZERO_ASSERT(entityIdx > 0 && entityIdx < MaxEntities,
            "DispatchRemove: invalid entity index");
        if (entityIdx == 0 || entityIdx >= MaxEntities) return;

        const uint8_t cid = cmd.componentId;
        PoolBase* pool = (cid < MaxComponents) ? m_poolPtr[cid].load(std::memory_order_acquire) : nullptr;
        if (!pool) return;

        // Pass the full Entity handle so the pool populates its typed tombstone
        // before swap-and-pop removes the component value.
        pool->destroyEntityFn(pool, cmd.entity);
        m_componentMask[entityIdx].Clear(cid);
    }

    // DispatchDestroy
    void EcsRegistry::DispatchDestroy(Entity e)
    {
        // Generation check — a stale destroy must never destroy the recycled
        // entity currently occupying the handle's slot.
        if (!m_allocator->IsAlive(e)) return;

        const uint32_t entityIdx = Index(e);
        ZERO_ASSERT(entityIdx > 0 && entityIdx < MaxEntities,
            "DispatchDestroy: invalid entity index");
        if (entityIdx == 0 || entityIdx >= MaxEntities) return;

        m_destroyedEntities.push_back(e);

        const ComponentMask& mask = m_componentMask[entityIdx];

        // Remove from every owned pool BEFORE FinalizeDestroy increments the
        // generation, capturing typed tombstones with component handle values.
        uint64_t lo = mask.lo;
        while (lo != 0)
        {
            const uint32_t bit = static_cast<uint32_t>(std::countr_zero(lo));
            if (PoolBase* pool = m_poolPtr[bit].load(std::memory_order_acquire))
                pool->destroyEntityFn(pool, e);
            lo &= (lo - 1u);
        }

        uint64_t hi = mask.hi;
        while (hi != 0)
        {
            const uint32_t bit = 64u + static_cast<uint32_t>(std::countr_zero(hi));
            if (PoolBase* pool = m_poolPtr[bit].load(std::memory_order_acquire))
                pool->destroyEntityFn(pool, e);
            hi &= (hi - 1u);
        }

        m_componentMask[entityIdx] = ComponentMask{};
        m_allocator->FinalizeDestroy(entityIdx);
    }

    // FlushCommands — main thread only
    //
    // 1. Dequeue and dispatch all pending EcsCommands.
    //    Tombstones accumulate across multiple flushes in a frame and are
    //    cleared only by ClearTombstones() at end-of-frame.
    // 2. Batched: invalidate dependent CachedViews on dirty pools, then
    //    explicitly rebuild each stale view once while still single-threaded.
    // 3. Reset slab (Invariant 9: only after all commands consumed).
    uint64_t EcsRegistry::FlushCommands(EcsCommandQueue& q)
    {
        uint64_t flushEpoch = 0;
        EcsCommand cmd;
        while (q.TryDequeue(cmd))
        {
            switch (cmd.type)
            {
            case EcsCommandType::AddComponent:
            case EcsCommandType::SetComponent:
                if (flushEpoch == 0)
                    flushEpoch = AdvanceEpoch();
                DispatchAdd(cmd, q, flushEpoch);
                break;
            case EcsCommandType::RemoveComponent:
                DispatchRemove(cmd);
                break;
            case EcsCommandType::DestroyEntity:
                DispatchDestroy(cmd.entity);
                break;
            }
        }

        // Pass 1: batched invalidation — O(MaxComponents = 128).
        for (uint32_t i = 0; i < MaxComponents; ++i)
        {
            PoolBase* pool = m_poolPtr[i].load(std::memory_order_acquire);
            if (!pool || !pool->structurallyDirty) continue;
            InvalidateViews(pool);
        }

        // Pass 2: explicit single-threaded rebuild of stale CachedViews so
        // concurrent Each() / Count() calls during the job window are pure reads.
        for (uint32_t i = 0; i < MaxComponents; ++i)
        {
            PoolBase* pool = m_poolPtr[i].load(std::memory_order_acquire);
            if (!pool || !pool->structurallyDirty) continue;
            for (CachedViewBase* v : pool->dependentViews)
            {
                if (v && v->IsStale() && v->rebuildFn)
                    v->rebuildFn(v);
            }
            pool->structurallyDirty = false;
        }

        // Invariant 9: slab reset only after every command payload has been read.
        q.slab.Reset();

#ifdef ECS_VALIDATE
        ValidateAll();
#endif

        return (flushEpoch != 0) ? flushEpoch : CurrentEpoch();
    }

    // -----------------------------------------------------------------------
    // ClearTombstones — main thread only, end-of-frame
    // -----------------------------------------------------------------------
    void EcsRegistry::ClearTombstones() noexcept
    {
        m_destroyedEntities.clear();

        for (uint32_t i = 0; i < MaxComponents; ++i)
        {
            PoolBase* pool = m_poolPtr[i].load(std::memory_order_acquire);
            if (!pool || !pool->hasTombstones) continue;
            if (pool->clearTombstonesFn)
                pool->clearTombstonesFn(pool);
        }
    }

#ifdef ECS_VALIDATE
    void EcsRegistry::ValidateAll() const
    {
        for (const auto& poolPtr : m_pools)
        {
            if (!poolPtr || !poolPtr->validateFn) continue;
            poolPtr->validateFn(poolPtr.get());
        }
    }
#endif
}
