#pragma once
#include <cstdint>
#include <array>
#include <vector>
#include <span>
#include <memory>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <cstring>
#include <tuple>
#include <utility>
#include <Engine/Core.h>
#include <Engine/Export.h>
#include <Engine/Ecs/Entity.h>
#include <Engine/Ecs/ComponentTrait.h>
#include <Engine/Ecs/ComponentMask.h>
#include <Engine/Ecs/EcsMask.h>
#include <Engine/Ecs/PoolBase.h>
#include <Engine/Ecs/ComponentPool.h>
#include <Engine/Ecs/EcsCommand.h>
#include <Engine/Ecs/CachedView.h>
#include <Engine/Ecs/ComponentView.h>
#include <Engine/Ecs/EcsCommandQueue.h>

// EntityAllocator is an internal type. Forward-declared here
namespace Zero { class EntityAllocator; }

namespace Zero
{
    // Central aggregator. Owns:
    //   - EntityAllocator (MPMC free list + atomic generation table) — opaque
    //   - ComponentMask per entity slot — heap-allocated (16 MiB, not on stack)
    //   - ComponentPool<T> table (128 slots, never reallocated)
    //   - Monotonic 64-bit dirty epoch counter (replaces frame-equality checks)
    //   - Entity & per-pool removal tombstones (accumulated across flushes,
    //     drained by subsystems, cleared at end-of-frame via ClearTombstones())
    //
    // Thread safety contract:
    //   FlushCommands() / ClearTombstones() — main thread only
    //   CreateEntity() / DestroyEntity()    — any thread (enqueues command)
    //   AddComponent / RemoveComponent      — any thread (enqueues command)
    //   Pool<T>() read access               — job window (topology immutable)
    //   GetMutable() / WriteView()          — job threads with declared write access
    class EcsRegistry
    {
    public:
        static constexpr uint32_t MaxComponents            = 128;
        static constexpr uint32_t MaxEntities              = 1u << 20; // mirrors EntityAllocator::MaxEntities
        static constexpr uint32_t DefaultComponentCapacity = 4096;

        ZERO_API EcsRegistry();
        ZERO_API ~EcsRegistry();

        // Non-copyable, non-movable — pools hand out stable raw references.
        EcsRegistry(const EcsRegistry&)            = delete;
        EcsRegistry& operator=(const EcsRegistry&) = delete;
        EcsRegistry(EcsRegistry&&)                 = delete;
        EcsRegistry& operator=(EcsRegistry&&)      = delete;

        // Any thread. Returns a live handle immediately.
        [[nodiscard]] ZERO_API Entity CreateEntity() noexcept;

        // Any thread. Enqueues destruction — executed during FlushCommands().
        ZERO_API void DestroyEntity(Entity e, EcsCommandQueue& q);

        // Any thread.
        [[nodiscard]] ZERO_API bool IsAlive(Entity e) const noexcept;

        // Any thread. Returns the full entity handle for a given index.
        // Used by ComponentView / CachedView to reconstruct handles during iteration.
        [[nodiscard]] ZERO_API Entity CurrentGeneration(uint32_t entityIdx) const noexcept;

        // Monotonic dirty epoch
        // Starts at 0; every AdvanceEpoch() returns a strictly larger value (>= 1).
        [[nodiscard]] uint64_t CurrentEpoch() const noexcept
        {
            return m_epoch.load(std::memory_order_acquire);
        }

        uint64_t AdvanceEpoch() noexcept
        {
            return m_epoch.fetch_add(1u, std::memory_order_acq_rel) + 1u;
        }

        // Fixed-timestep render interpolation factor in [0, 1].
        // Updated by Application::Run() after FixedStep and read by RenderSyncSystem.
        void SetInterpolationAlpha(float alpha) noexcept { m_interpolationAlpha = alpha; }
        [[nodiscard]] float InterpolationAlpha() const noexcept { return m_interpolationAlpha; }

        // Component commands (any thread — enqueued, flushed on main thread)
        template<typename T>
        void AddComponent(Entity e, T value, EcsCommandQueue& q)
        {
            ZERO_CORE_ASSERT(IsAlive(e), "AddComponent called on dead entity");
            GetOrCreatePool<T>();
            q.EnqueueAdd(e, std::move(value));
        }

        template<typename T>
        void SetComponent(Entity e, T value, EcsCommandQueue& q)
        {
            ZERO_CORE_ASSERT(IsAlive(e), "SetComponent called on dead entity");
            GetOrCreatePool<T>();
            q.EnqueueSet(e, std::move(value));
        }

        template<typename T>
        void RemoveComponent(Entity e, EcsCommandQueue& q)
        {
            ZERO_CORE_ASSERT(IsAlive(e), "RemoveComponent called on dead entity");
            q.EnqueueRemove(e, static_cast<uint8_t>(ComponentId<T>()));
        }

        // Pool access (main thread or read-only job access)
        template<typename T>
        [[nodiscard]] ComponentPool<T>& Pool()
        {
            constexpr uint32_t id = ComponentId<T>();
            if (PoolBase* p = m_poolPtr[id].load(std::memory_order_acquire))
                return *static_cast<ComponentPool<T>*>(p);
            return GetOrCreatePool<T>();
        }

        template<typename T>
        [[nodiscard]] const ComponentPool<T>& Pool() const
        {
            constexpr uint32_t id = ComponentId<T>();
            PoolBase* p = m_poolPtr[id].load(std::memory_order_acquire);
            if (!p)
            {
                ZERO_ASSERT(false,
                    "ComponentPool not created — was AddComponent<T> ever flushed?");
                static const ComponentPool<T> s_emptyPool{};
                return s_emptyPool;
            }
            return *static_cast<const ComponentPool<T>*>(p);
        }

        [[nodiscard]] ZERO_API const ComponentMask& Mask(uint32_t entityIdx) const noexcept;

        // Read-only span of all entities destroyed since the last ClearTombstones().
        // For component-specific handle values (e.g., RenderableHandleComponent,
        // RigidBodyComponent), inspect reg.Pool<T>().Removed().
        [[nodiscard]] std::span<const Entity> DestroyedEntities() const noexcept
        {
            return { m_destroyedEntities.data(), m_destroyedEntities.size() };
        }

        // Structural flush — main thread only.
        // Populates per-pool and entity destruction tombstones (does NOT clear them).
        // Returns the registry epoch stamped on any Add/Set commands (or CurrentEpoch()).
        ZERO_API uint64_t FlushCommands(EcsCommandQueue& q);

        // End-of-frame tombstone clear — main thread only.
        // Clears m_destroyedEntities and all pools' removedEntities buffers while
        // preserving capacity (zero steady-state heap allocations).
        ZERO_API void ClearTombstones() noexcept;

#ifdef ECS_VALIDATE
        ZERO_API void ValidateAll() const;
#endif

    private:
        void DispatchAdd(const EcsCommand& cmd, EcsCommandQueue& q, uint64_t epoch);
        void DispatchRemove(const EcsCommand& cmd);
        void DispatchDestroy(Entity e);

        template<typename T>
        ComponentPool<T>& GetOrCreatePool()
        {
            constexpr uint32_t id = ComponentId<T>();
            if (PoolBase* p = m_poolPtr[id].load(std::memory_order_acquire))
                return *static_cast<ComponentPool<T>*>(p);

            std::lock_guard<std::mutex> lock(m_poolMutex);
            if (PoolBase* p = m_poolPtr[id].load(std::memory_order_acquire))
                return *static_cast<ComponentPool<T>*>(p);

            auto* pool = new ComponentPool<T>();
            pool->componentId = id;
            pool->Reserve(MaxEntities, DefaultComponentCapacity);

            pool->destroyEntityFn = [](PoolBase* base, Entity entity) {
                static_cast<ComponentPool<T>*>(base)->Remove(entity);
            };
            pool->denseEntityAt = [](const PoolBase* base, uint32_t denseIdx) {
                return static_cast<const ComponentPool<T>*>(base)->dense_to_entity[denseIdx];
            };
            // Upsert semantics: AddComponent/SetComponent on an entity that
            // already has this component overwrites the value instead of
            // asserting / corrupting the sparse set.
            pool->insertFn = [](PoolBase* base, uint32_t entityIdx,
                                const void* payload, uint64_t epoch, WriteSource source)
            {
                auto* p = static_cast<ComponentPool<T>*>(base);
                if (p->Has(entityIdx))
                {
                    const uint32_t d = p->sparse[entityIdx];
                    T value;
                    std::memcpy(&value, payload, sizeof(T));
                    p->data[d]           = value;
                    p->modifiedEpoch[d]  = epoch;
                    p->modifiedSource[d] = source;
                    return;
                }
                T value;
                std::memcpy(&value, payload, sizeof(T));
                p->Insert(entityIdx, value, epoch, source);
            };
            pool->resetFn = [](PoolBase* base) {
                auto* p = static_cast<ComponentPool<T>*>(base);
                p->data.clear();
                p->dense_to_entity.clear();
                p->modifiedEpoch.clear();
                p->modifiedSource.clear();
                p->removedEntities.clear();
                p->sparse.assign(p->sparse.size(), UINT32_MAX);
                p->count             = 0;
                p->structurallyDirty = true;
                p->hasTombstones     = false;
            };
            pool->clearTombstonesFn = [](PoolBase* base) {
                static_cast<ComponentPool<T>*>(base)->ClearTombstones();
            };
#ifdef ECS_VALIDATE
            pool->validateFn = [](const PoolBase* base) {
                static_cast<const ComponentPool<T>*>(base)->ValidateStructure();
            };
#endif

            // Take ownership before publishing the atomic pointer so readers
            // never observe a pool before m_pools[id] is populated.
            m_pools[id].reset(pool);
            m_poolPtr[id].store(pool, std::memory_order_release);
            return *pool;
        }

        std::mutex m_poolMutex;

        // Internal allocator
        std::unique_ptr<EntityAllocator>                  m_allocator;

        // 1M * 16 bytes = 16 MiB
        std::unique_ptr<ComponentMask[]>                  m_componentMask;

        // Pool table — 128 slots, sized once at construction, never reallocated.
        std::vector<std::unique_ptr<PoolBase>>            m_pools;
        std::array<std::atomic<PoolBase*>, MaxComponents> m_poolPtr{};

        // Monotonic dirty tracking epoch and fixed-step interpolation alpha.
        std::atomic<uint64_t>                             m_epoch{ 0 };
        float                                             m_interpolationAlpha = 1.0f;

        // Registry-level entity destruction tombstones (cleared in ClearTombstones()).
        std::vector<Entity>                               m_destroyedEntities;

        template<typename...> friend class CachedView;
        template<typename...> friend class ComponentView;
    };

    // CachedView<Ts...> out-of-line method definitions
    // Must appear after the full EcsRegistry definition.

    namespace Detail
    {
        inline void RebuildCachedView(CachedViewBase* base)
        {
            if (!base || !base->reg || base->pools.empty())
                return;

            base->primary = *std::min_element(base->pools.begin(), base->pools.end(),
                [](const PoolBase* a, const PoolBase* b) { return a->count < b->count; });

            base->entities.clear();
            const uint32_t n = base->primary->count;
            base->entities.reserve(n);

            for (uint32_t i = 0; i < n; ++i)
            {
                const uint32_t entityIdx = base->primary->denseEntityAt(base->primary, i);
                if (base->reg->Mask(entityIdx).Contains(base->required))
                    base->entities.push_back(entityIdx);
            }
            base->MarkRebuilt();
        }
    }

    template<typename... Ts>
    CachedView<Ts...>::CachedView(EcsRegistry& reg)
        : m_base(std::make_unique<CachedViewBase>())
    {
        m_base->reg       = &reg;
        m_base->required  = MaskOf<Ts...>();
        m_base->rebuildFn = &Detail::RebuildCachedView;

        auto attachPool = [this](PoolBase* p) {
            if (std::find(m_base->pools.begin(), m_base->pools.end(), p) == m_base->pools.end())
            {
                m_base->pools.push_back(p);
                p->dependentViews.push_back(m_base.get());
            }
        };
        (attachPool(&reg.Pool<Ts>()), ...);

        Detail::RebuildCachedView(m_base.get());
    }

    // ComponentView<Ts...> out-of-line method definitions

    template<typename... Ts>
    ComponentView<Ts...>::ComponentView(EcsRegistry& reg)
        : m_reg(reg)
        , m_required(MaskOf<Ts...>())
        , m_pools({ &reg.Pool<Ts>()... })
    {
        m_primary = *std::min_element(m_pools.begin(), m_pools.end(),
            [](const PoolBase* a, const PoolBase* b) { return a->count < b->count; });
    }

    template<typename... Ts>
    template<size_t PrimaryIdx, typename Fn, size_t... Is>
    void ComponentView<Ts...>::EachWithPrimary(Fn& fn, std::index_sequence<Is...>) const
    {
        using PrimaryT = std::tuple_element_t<PrimaryIdx, std::tuple<Ts...>>;
        const auto* primaryPool   = static_cast<const ComponentPool<PrimaryT>*>(m_pools[PrimaryIdx]);
        const uint32_t* denseEnts = primaryPool->dense_to_entity.data();
        const uint32_t n          = primaryPool->count;

        for (uint32_t i = 0; i < n; ++i)
        {
            const uint32_t entityIdx = denseEnts[i];
            if (!m_reg.Mask(entityIdx).Contains(m_required)) continue;

            fn(m_reg.CurrentGeneration(entityIdx),
               ([&]() -> const Ts& {
                   if constexpr (Is == PrimaryIdx)
                       return primaryPool->data[i];
                   else
                       return *static_cast<const ComponentPool<Ts>*>(m_pools[Is])->Get(entityIdx);
               }())...);
        }
    }

    template<typename... Ts>
    template<typename Fn>
    void ComponentView<Ts...>::Each(Fn&& fn)
    {
        [&]<size_t... Ps>(std::index_sequence<Ps...>) {
            ((m_primary == m_pools[Ps]
                ? (EachWithPrimary<Ps>(fn, std::index_sequence_for<Ts...>{}), true)
                : false) || ...);
        }(std::index_sequence_for<Ts...>{});
    }

    template<typename... Ts>
    template<size_t PrimaryIdx, typename... Excludes, typename Fn, size_t... Is>
    void ComponentView<Ts...>::EachExcludingWithPrimary(Fn& fn, ComponentMask excludeMask, std::index_sequence<Is...>) const
    {
        using PrimaryT = std::tuple_element_t<PrimaryIdx, std::tuple<Ts...>>;
        const auto* primaryPool   = static_cast<const ComponentPool<PrimaryT>*>(m_pools[PrimaryIdx]);
        const uint32_t* denseEnts = primaryPool->dense_to_entity.data();
        const uint32_t n          = primaryPool->count;

        for (uint32_t i = 0; i < n; ++i)
        {
            const uint32_t entityIdx = denseEnts[i];
            const ComponentMask& em  = m_reg.Mask(entityIdx);
            if (!em.Contains(m_required))    continue;
            if (em.ContainsAny(excludeMask)) continue;

            fn(m_reg.CurrentGeneration(entityIdx),
               ([&]() -> const Ts& {
                   if constexpr (Is == PrimaryIdx)
                       return primaryPool->data[i];
                   else
                       return *static_cast<const ComponentPool<Ts>*>(m_pools[Is])->Get(entityIdx);
               }())...);
        }
    }

    template<typename... Ts>
    template<typename... Excludes, typename Fn>
    void ComponentView<Ts...>::EachExcluding(Fn&& fn)
    {
        constexpr ComponentMask excludeMask = MaskOf<Excludes...>();
        [&]<size_t... Ps>(std::index_sequence<Ps...>) {
            ((m_primary == m_pools[Ps]
                ? (EachExcludingWithPrimary<Ps, Excludes...>(fn, excludeMask, std::index_sequence_for<Ts...>{}), true)
                : false) || ...);
        }(std::index_sequence_for<Ts...>{});
    }

    template<typename... Ts>
    uint32_t ComponentView<Ts...>::Count() const noexcept
    {
        if constexpr (sizeof...(Ts) == 1)
            return m_primary->count;

        uint32_t matches = 0;
        const uint32_t n = m_primary->count;
        for (uint32_t i = 0; i < n; ++i)
        {
            const uint32_t entityIdx = m_primary->denseEntityAt(m_primary, i);
            if (m_reg.Mask(entityIdx).Contains(m_required))
                ++matches;
        }
        return matches;
    }
}

// Application-side integration (PushComponent / PushSystem endpoints).
// Included last so EcsRegistry is complete before EcsIntegration instantiates.
#include <Engine/Ecs/ApplicationIntegration.h>
