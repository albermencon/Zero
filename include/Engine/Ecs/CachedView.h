#pragma once
#include <cstdint>
#include <vector>
#include <memory>
#include <algorithm>
#include "PoolBase.h"
#include "ComponentMask.h"

namespace Zero
{
    // Stable heap object owned by CachedView<Ts...>.
    // Pools hold CachedViewBase* — not raw version counters.
    // This lets the pool call Invalidate() without knowing the view's type.
    struct CachedViewBase
    {
        using RebuildFn = void(*)(CachedViewBase*);

        std::vector<uint32_t>  entities;            // rebuilt on construction & FlushCommands()
        uint64_t               viewVersion  = 0;    // version at last successful Rebuild()
        uint64_t               poolVersion  = 1;    // incremented by pool on structural change
        bool                   needsRebuild = true; // starts stale — forces first build

        const class EcsRegistry* reg       = nullptr;
        ComponentMask            required{};
        PoolBase*                primary   = nullptr;
        RebuildFn                rebuildFn = nullptr;
        std::vector<PoolBase*>   pools;             // pools this view is registered with

        CachedViewBase() = default;
        ~CachedViewBase() { DetachAll(); }

        CachedViewBase(const CachedViewBase&)            = delete;
        CachedViewBase& operator=(const CachedViewBase&) = delete;
        CachedViewBase(CachedViewBase&&)                 = delete;
        CachedViewBase& operator=(CachedViewBase&&)      = delete;

        void DetachAll() noexcept
        {
            for (PoolBase* p : pools)
            {
                if (!p) continue;
                auto& v = p->dependentViews;
                v.erase(std::remove(v.begin(), v.end(), this), v.end());
            }
            pools.clear();
            primary   = nullptr;
            reg       = nullptr;
            rebuildFn = nullptr;
        }

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

    // Helper: extract first type from pack
    template<typename First, typename...>
    struct FirstOf { using type = First; };

    // Heap-resident view that caches a filtered entity list across frames.
    // Built on construction and rebuilt explicitly during single-threaded
    // EcsRegistry::FlushCommands() so concurrent Each() / Count() reads are race-free.
    template<typename... Ts>
    class CachedView
    {
    public:
        explicit CachedView(class EcsRegistry& reg);

        // Read-only iteration over cached entity indices — safe for concurrent workers.
        template<typename Fn>
        void Each(Fn&& fn) const
        {
            for (const uint32_t idx : m_base->entities)
                fn(idx);
        }

        [[nodiscard]] uint32_t Count() const noexcept
        {
            return static_cast<uint32_t>(m_base->entities.size());
        }

    private:
        std::unique_ptr<CachedViewBase> m_base;
    };
}
