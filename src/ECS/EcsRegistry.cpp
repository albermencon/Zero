#include "pch.h"
#include <Engine/Ecs/EcsRegistry.h>
#include <Engine/Ecs/CachedView.h>
#include "EntityAllocator.h"

namespace Zero
{
    // -----------------------------------------------------------------------
    // EcsRegistry — constructor / destructor
    // -----------------------------------------------------------------------
    EcsRegistry::EcsRegistry()
        : m_allocator(std::make_unique<EntityAllocator>())
        , m_componentMask(std::make_unique<ComponentMask[]>(MaxEntities))
    {
        // Resize once — all unique_ptrs default to nullptr.
        // Never reallocated: all PoolBase* references handed to views remain stable.
        m_pools.resize(MaxComponents);
    }

    // unique_ptr members require a destructor definition here because EntityAllocator
    // is an incomplete type in the public header.
    EcsRegistry::~EcsRegistry() = default;

    // -----------------------------------------------------------------------
    // Entity lifecycle
    // -----------------------------------------------------------------------
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

    // -----------------------------------------------------------------------
    // Mask access
    // -----------------------------------------------------------------------
    const ComponentMask& EcsRegistry::Mask(uint32_t entityIdx) const noexcept
    {
        ZERO_ASSERT(entityIdx < MaxEntities, "entityIdx out of range");
        return m_componentMask[entityIdx];
    }

    // -----------------------------------------------------------------------
    // Static file-local helper — calls CachedViewBase::Invalidate() without
    // knowing T. Lives here because CachedView.h is fully included in this TU.
    // -----------------------------------------------------------------------
    static void InvalidateViews(PoolBase* base) noexcept
    {
        for (CachedViewBase* v : base->dependentViews)
            v->Invalidate();
    }

    // -----------------------------------------------------------------------
    // DispatchAdd
    // -----------------------------------------------------------------------
    void EcsRegistry::DispatchAdd(const EcsCommand& cmd, EcsCommandQueue& q, uint32_t currentFrame)
    {
        // Generation check: a stale handle (entity destroyed and its slot
        // recycled since the command was enqueued) must be dropped, not applied
        // to the recycled slot.
        if (!m_allocator->IsAlive(cmd.entity)) return;

        const uint32_t entityIdx = Index(cmd.entity);
        ZERO_CORE_ASSERT(entityIdx > 0 && entityIdx < MaxEntities,
            "DispatchAdd: invalid entity index");

        const uint8_t cid = cmd.componentId;
        ZERO_ASSERT(cid < MaxComponents, "DispatchAdd: componentId out of range");
        ZERO_ASSERT(m_pools[cid] != nullptr,
            "DispatchAdd: pool does not exist — GetOrCreatePool<T> must be called first");

        PoolBase* pool = m_pools[cid].get();

        const void* payload;
        if (cmd.flags & kEcsFlagSlabPayload)
            payload = static_cast<const void*>(&q.slab.As<uint8_t>(cmd.slabOffset));
        else if (cmd.flags & kEcsFlagHeapPayload)
            payload = cmd.heapPtr;
        else
            payload = static_cast<const void*>(cmd.inlineData);

        pool->insertFn(pool, entityIdx, payload, currentFrame);
        m_componentMask[entityIdx].Set(cid);

        // Free heap-fallback payloads after they have been copied into the pool.
        if (cmd.flags & kEcsFlagHeapPayload)
        {
            ::operator delete(const_cast<void*>(cmd.heapPtr),
                              std::align_val_t{ cmd.heapAlign });
        }
    }

    // -----------------------------------------------------------------------
    // DispatchRemove
    // -----------------------------------------------------------------------
    void EcsRegistry::DispatchRemove(const EcsCommand& cmd)
    {
        // Generation check — never touch a recycled slot for a dead handle.
        if (!m_allocator->IsAlive(cmd.entity)) return;

        const uint32_t entityIdx = Index(cmd.entity);
        ZERO_ASSERT(entityIdx > 0 && entityIdx < MaxEntities,
            "DispatchRemove: invalid entity index");

        const uint8_t cid = cmd.componentId;
        if (cid >= MaxComponents || !m_pools[cid]) return;

        m_pools[cid]->destroyEntityFn(m_pools[cid].get(), entityIdx);
        m_componentMask[entityIdx].Clear(cid);
    }

    // -----------------------------------------------------------------------
    // DispatchDestroy
    // -----------------------------------------------------------------------
    void EcsRegistry::DispatchDestroy(Entity e)
    {
        // Generation check — a stale destroy must never destroy the recycled
        // entity currently occupying the handle's slot.
        if (!m_allocator->IsAlive(e)) return;

        const uint32_t entityIdx = Index(e);
        ZERO_ASSERT(entityIdx > 0 && entityIdx < MaxEntities,
            "DispatchDestroy: invalid entity index");

        const ComponentMask& mask = m_componentMask[entityIdx];
        for (uint32_t id = 0; id < MaxComponents; ++id)
        {
            if (!mask.Has(id) || !m_pools[id]) continue;
            m_pools[id]->destroyEntityFn(m_pools[id].get(), entityIdx);
        }

        m_componentMask[entityIdx] = ComponentMask{};
        m_allocator->FinalizeDestroy(entityIdx);
    }

    // -----------------------------------------------------------------------
    // FlushCommands — main thread only
    //
    // 1. Dequeue and dispatch all pending EcsCommands.
    // 2. Batched: iterate only dirty pools, call InvalidateViews once per pool.
    // 3. Reset slab (Invariant 9: only after all commands consumed).
    // -----------------------------------------------------------------------
    void EcsRegistry::FlushCommands(EcsCommandQueue& q, uint32_t currentFrame)
    {
        EcsCommand cmd;
        while (q.queue.try_dequeue(cmd))
        {
            switch (cmd.type)
            {
            case EcsCommandType::AddComponent:
            case EcsCommandType::SetComponent:
                DispatchAdd(cmd, q, currentFrame);
                break;
            case EcsCommandType::RemoveComponent:
                DispatchRemove(cmd);
                break;
            case EcsCommandType::DestroyEntity:
                DispatchDestroy(cmd.entity);
                break;
            }
        }

        // Batched invalidation — O(MaxComponents = 128), once per frame.
        // Invariant 7: Invalidate() only bumps a version counter, never rebuilds.
        for (auto& poolPtr : m_pools)
        {
            if (!poolPtr || !poolPtr->structurallyDirty) continue;
            InvalidateViews(poolPtr.get());
            poolPtr->structurallyDirty = false;
        }

        // Invariant 9: slab reset only after every command payload has been read.
        q.slab.Reset();

#ifdef ECS_VALIDATE
        ValidateAll();
#endif
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
