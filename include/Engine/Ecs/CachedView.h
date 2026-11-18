#pragma once
#include <cstdint>
#include <vector>
#include <memory>
#include <algorithm>
#include "PoolBase.h"
#include "ComponentMask.h"

namespace Zero
{
    // CachedViewBase
    //
    // Stable heap object owned by CachedView<Ts...>.
    // Pools hold CachedViewBase* — not raw version counters.
    // This lets the pool call Invalidate() without knowing the view's type.
    //
    // Invariant 7: a view is EITHER current (viewVersion == poolVersion)
    //              OR stale (needsRebuild == true) — never partially rebuilt.
    struct CachedViewBase
    {
        std::vector<uint32_t> entities;     // rebuilt lazily
        uint64_t viewVersion  = 0;          // version at last successful Rebuild()
        uint64_t poolVersion  = 1;          // incremented by pool on structural change
        bool     needsRebuild = true;       // starts stale — forces first build

        void Invalidate() noexcept
        {
            ++poolVersion;
            needsRebuild = true;
        }

        [[nodiscard]] bool IsStale() const noexcept { return viewVersion != poolVersion; }

        void MarkRebuilt() noexcept
        {
            viewVersion  = poolVersion;
            needsRebuild = false;
        }
    };

    // CachedViewRegistration
    //
    // RAII guard: pushes CachedViewBase* into pool.dependentViews on construction,
    // erases it on destruction. CachedView<Ts...> owns one per involved pool.
    // Move-constructible so it can live inside std::vector.
    class CachedViewRegistration
    {
    public:
        CachedViewRegistration(PoolBase& pool, CachedViewBase& base)
            : m_pool(&pool), m_base(&base)
        {
            m_pool->dependentViews.push_back(m_base);
        }

        ~CachedViewRegistration()
        {
            if (!m_pool) return;
            auto& v = m_pool->dependentViews;
            v.erase(std::remove(v.begin(), v.end(), m_base), v.end());
        }

        CachedViewRegistration(CachedViewRegistration&& other) noexcept
            : m_pool(other.m_pool), m_base(other.m_base)
        {
            other.m_pool = nullptr;
            other.m_base = nullptr;
        }

        CachedViewRegistration& operator=(CachedViewRegistration&&) = delete;
        CachedViewRegistration(const CachedViewRegistration&)       = delete;
        CachedViewRegistration& operator=(const CachedViewRegistration&) = delete;

    private:
        PoolBase*       m_pool;
        CachedViewBase* m_base;
    };

    // Helper: extract first type from pack
    template<typename First, typename...>
    struct FirstOf { using type = First; };

    // CachedView<Ts...>
    //
    // Heap-resident view that caches a filtered entity list across frames.
    // Rebuild happens lazily on the first Each() call after a structural change.
    //
    // EcsRegistry must be a friend to access m_componentMask and Pool<T>().
    template<typename... Ts>
    class CachedView
    {
    public:
        explicit CachedView(class EcsRegistry& reg);

        // Iterate cached entity indices. Triggers a lazy rebuild if stale.
        template<typename Fn>
        void Each(Fn&& fn)
        {
            if (m_base->IsStale()) Rebuild();
            for (const uint32_t idx : m_base->entities)
                fn(idx);
        }

        [[nodiscard]] uint32_t Count()
        {
            if (m_base->IsStale()) Rebuild();
            return static_cast<uint32_t>(m_base->entities.size());
        }

    private:
        void Rebuild();

        class EcsRegistry&                       m_reg;
        ComponentMask                            m_required;
        PoolBase*                                m_primary = nullptr;
        std::unique_ptr<CachedViewBase>          m_base;
        std::vector<CachedViewRegistration>      m_registrations;
    };
}
