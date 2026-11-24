#pragma once
#include <cstddef>
#include <cstdint>
#include <array>
#include <utility>
#include "PoolBase.h"
#include "ComponentMask.h"
#include "CachedView.h"   // FirstOf helper

namespace Zero
{
    // Lightweight, stack-allocated view. Not cached — rebuilds iteration on
    // every Each() call by walking the smallest pool's dense array.
    //
    // Use this for one-shot queries where pool sizes change frequently.
    // Use CachedView<Ts...> for recurring queries that benefit from caching.
    template<typename... Ts>
    class ComponentView
    {
        static_assert(sizeof...(Ts) > 0, "ComponentView requires at least one component type");

    public:
        explicit ComponentView(class EcsRegistry& reg);

        // Calls fn(Entity, const Ts&...) for every entity that owns all Ts.
        // fn receives a full Entity handle (index + generation).
        template<typename Fn>
        void Each(Fn&& fn);

        // Same as Each but additionally skips entities that own any Exclude type.
        template<typename... Excludes, typename Fn>
        void EachExcluding(Fn&& fn);

        // Exact number of entities matching the full mask — not the size of the
        // smallest pool (which can overcount when some entities in the primary
        // pool lack one of the required components).
        [[nodiscard]] uint32_t Count() const noexcept;

    private:
        template<size_t PrimaryIdx, typename Fn, size_t... Is>
        void EachWithPrimary(Fn& fn, std::index_sequence<Is...>) const;

        template<size_t PrimaryIdx, typename... Excludes, typename Fn, size_t... Is>
        void EachExcludingWithPrimary(Fn& fn, ComponentMask excludeMask, std::index_sequence<Is...>) const;

        class EcsRegistry&                         m_reg;
        const ComponentMask                        m_required;
        std::array<PoolBase*, sizeof...(Ts)>       m_pools;
        PoolBase*                                  m_primary = nullptr;
    };
}
