#pragma once
#include <cstdint>
#include <array>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <cstring>
#include <tuple>
#include <utility>
#include <Engine/Core.h>
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

// EntityAllocator is an internal type. Forward-declared here so EcsRegistry can
// hold it by unique_ptr without exposing its definition to public consumers.
namespace Zero { class EntityAllocator; }

namespace Zero
{
    // EcsRegistry
    //
    // Central aggregator. Owns:
    //   - EntityAllocator (MPMC free list + atomic generation table) — opaque
    //   - ComponentMask per entity slot — heap-allocated (16 MiB, not on stack)
    //   - ComponentPool<T> table (128 slots, never reallocated)
    //
    // Thread safety contract:
    //   FlushCommands()                  — main thread only
    //   CreateEntity() / DestroyEntity() — any thread (enqueues command)
    //   AddComponent / RemoveComponent   — any thread (enqueues command)
    //   Pool<T>() read access            — job window (topology immutable)
    //   GetMutable()                     — job threads with declared write access
    //
    // Pool creation is guarded by a mutex AND an atomic pointer mirror so the
    // first concurrent AddComponent for a new component type is safe to call
    // from any thread.
    class EcsRegistry
    {
    public:
        static constexpr uint32_t MaxComponents         = 128;
        static constexpr uint32_t MaxEntities           = 1u << 20; // mirrors EntityAllocator::MaxEntities
        static constexpr uint32_t DefaultComponentCapacity = 4096;

        EcsRegistry();
        ~EcsRegistry();

        // Non-copyable, non-movable — pools hand out stable raw references.
        EcsRegistry(const EcsRegistry&)            = delete;
        EcsRegistry& operator=(const EcsRegistry&) = delete;
        EcsRegistry(EcsRegistry&&)                 = delete;
        EcsRegistry& operator=(EcsRegistry&&)      = delete;

        // Entity lifecycle

        // Any thread. Returns a live handle immediately.
        [[nodiscard]] Entity CreateEntity() noexcept;

        // Any thread. Enqueues destruction — executed during FlushCommands().
        void DestroyEntity(Entity e, EcsCommandQueue& q);

        // Any thread.
        [[nodiscard]] bool IsAlive(Entity e) const noexcept;

        // Any thread. Returns the full entity handle for a given index.
        // Used by ComponentView / CachedView to reconstruct handles during iteration.
        [[nodiscard]] Entity CurrentGeneration(uint32_t entityIdx) const noexcept;

        // Component commands (any thread — enqueued, flushed on main thread)
        template<typename T>
        void AddComponent(Entity e, T value, EcsCommandQueue& q)
        {
            ZERO_CORE_ASSERT(IsAlive(e), "AddComponent called on dead entity");
            GetOrCreatePool<T>();
            q.EnqueueAdd(e, std::move(value));
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

        [[nodiscard]] const ComponentMask& Mask(uint32_t entityIdx) const noexcept;

        // Frame flush — main thread only
        void FlushCommands(EcsCommandQueue& q, uint32_t currentFrame);

#ifdef ECS_VALIDATE
        void ValidateAll() const;
#endif

    private:
        void DispatchAdd(const EcsCommand& cmd, EcsCommandQueue& q, uint32_t currentFrame);
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

            pool->destroyEntityFn = [](PoolBase* base, uint32_t entityIdx) {
                static_cast<ComponentPool<T>*>(base)->Remove(entityIdx);
            };
            pool->denseEntityAt = [](const PoolBase* base, uint32_t denseIdx) {
                return static_cast<const ComponentPool<T>*>(base)->dense_to_entity[denseIdx];
            };
            // Upsert semantics: AddComponent/SetComponent on an entity that
            // already has this component overwrites the value instead of
            // asserting / corrupting the sparse set. (SetComponent & duplicate
            // AddComponent both land here.)
            pool->insertFn = [](PoolBase* base, uint32_t entityIdx,
                                const void* payload, uint32_t frame)
            {
                auto* p = static_cast<ComponentPool<T>*>(base);
                if (p->Has(entityIdx))
                {
                    const uint32_t d = p->sparse[entityIdx];
                    T value;
                    std::memcpy(&value, payload, sizeof(T));
                    p->data[d] = value;
                    p->modifiedFrame[d] = frame;
                    return;
                }
                T value;
                std::memcpy(&value, payload, sizeof(T));
                p->Insert(entityIdx, value, frame);
            };
            pool->resetFn = [](PoolBase* base) {
                auto* p = static_cast<ComponentPool<T>*>(base);
                p->data.clear();
                p->dense_to_entity.clear();
                p->modifiedFrame.clear();
                p->sparse.assign(p->sparse.size(), UINT32_MAX);
                p->count = 0;
                p->structurallyDirty = true;
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
        std::unique_ptr<EntityAllocator>            m_allocator;

        // 1M * 16 bytes = 16 MiB
        std::unique_ptr<ComponentMask[]>            m_componentMask;

        // Pool table — 128 slots, sized once at construction, never reallocated.
        // All PoolBase* / ComponentPool<T>& references handed to views are stable.
        // m_pools owns the objects; m_poolPtr is the lock-free read mirror so
        // concurrent first-use AddComponent does not data-race on the unique_ptr.
        std::vector<std::unique_ptr<PoolBase>>      m_pools;
        std::array<std::atomic<PoolBase*>, MaxComponents> m_poolPtr{};

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

            // Reconstruct full entity handle from index + live generation,
            // indexing m_primary directly by dense slot i and reading cached m_pools.
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

        // Exact match count: walk the smallest pool and count entities whose
        // full mask contains every required component. m_primary->count alone
        // is only an upper bound when some primary-pool entities lack a
        // non-primary required component.
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
