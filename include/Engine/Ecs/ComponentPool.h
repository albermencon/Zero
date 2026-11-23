#pragma once
#include <cstdint>
#include <cassert>
#include <memory_resource>
#include <Engine/Core.h>
#include "PoolBase.h"

namespace Zero
{
    // ComponentPool<T>
    //
    // Sparse-set storage for one component type T.
    // T must be trivially copyable — no destructor, no copy overhead on swap-and-pop.
    //
    // Memory layout:
    //   HOT  (iterated every frame) : data[], dense_to_entity[]
    //   COLD (dirty tracking)       : modifiedFrame[]
    //   STRUCTURE (immutable during job window) : sparse[]
    //
    // PMR toggle: define ECS_NO_PMR to fall back to std::vector.

#ifndef ECS_NO_PMR
    template<typename U>
    using EcsVec = std::pmr::vector<U>;
#else
    template<typename U>
    using EcsVec = std::vector<U>;
#endif

    template<typename T>
        requires std::is_trivially_copyable_v<T>
    struct ComponentPool : PoolBase
    {
        // HOT
        EcsVec<T>        data;
        EcsVec<uint32_t> dense_to_entity;

        // COLD — dirty tracking
        // modifiedFrame[i] = frame number of the last write to dense slot i.
        // Never stored inside T — preserves the hot struct's cache footprint.
        EcsVec<uint32_t> modifiedFrame;

        // STRUCTURE — read-only during the job window
        // sparse[entityIdx] = dense index,  UINT32_MAX = not present
        EcsVec<uint32_t> sparse;

        // DirtyRef — RAII proxy returned by GetMutable().
        // Writes modifiedFrame exactly once, on scope exit.
        // Call only from job threads with declared write access to this pool.
        struct DirtyRef
        {
            T&        value;
            uint32_t& frame;
            uint32_t  currentFrame;

            DirtyRef(T& v, uint32_t& f, uint32_t cf) noexcept 
                : value(v), frame(f), currentFrame(cf) {}

            ~DirtyRef() { frame = currentFrame; }

            DirtyRef(const DirtyRef&) = delete;
            DirtyRef& operator=(const DirtyRef&) = delete;

            T* operator->() noexcept { return &value; }
            T& operator*()  noexcept { return value; }
        };

        // Reserve
        // Call once at pool construction before any Insert.
        // entityCapacity  — must cover the maximum entity index.
        // componentCapacity — initial reserve for the dense arrays.
#ifndef ECS_NO_PMR
        void Reserve(uint32_t entityCapacity, uint32_t componentCapacity,
                     std::pmr::memory_resource* mr = std::pmr::get_default_resource())
        {
            sparse         = EcsVec<uint32_t>(mr);
            data           = EcsVec<T>(mr);
            dense_to_entity = EcsVec<uint32_t>(mr);
            modifiedFrame  = EcsVec<uint32_t>(mr);

            sparse.assign(entityCapacity, UINT32_MAX);
            data.reserve(componentCapacity);
            dense_to_entity.reserve(componentCapacity);
            modifiedFrame.reserve(componentCapacity);
        }
#else
        void Reserve(uint32_t entityCapacity, uint32_t componentCapacity)
        {
            sparse.assign(entityCapacity, UINT32_MAX);
            data.reserve(componentCapacity);
            dense_to_entity.reserve(componentCapacity);
            modifiedFrame.reserve(componentCapacity);
        }
#endif

        // Insert — main thread only, inside FlushCommands().
        // Invariant 1: all three dense arrays grow atomically.
        // Invariant 2: entity must not already be present.
        // Sets structurallyDirty; view invalidation is batched post-flush.
        void Insert(uint32_t entityIdx, T value, uint32_t currentFrame)
        {
            ZERO_CORE_ASSERT(entityIdx < sparse.size(), "entityIdx out of sparse bounds");
            ZERO_CORE_ASSERT(sparse[entityIdx] == UINT32_MAX, "Entity already has this component");

            sparse[entityIdx] = static_cast<uint32_t>(data.size());
            dense_to_entity.push_back(entityIdx);
            data.push_back(std::move(value));
            modifiedFrame.push_back(currentFrame);
            ++count;
            structurallyDirty = true;
        }

        // Remove — main thread only, inside FlushCommands().
        // O(1) swap-and-pop. Keeps dense arrays packed.
        // Invariant 3: fixes up sparse[movedEntity] after the swap.
        void Remove(uint32_t entityIdx)
        {
            ZERO_CORE_ASSERT(entityIdx < sparse.size(), "entityIdx out of sparse bounds");
            const uint32_t denseIdx = sparse[entityIdx];
            if (denseIdx == UINT32_MAX) return;

            const uint32_t last = static_cast<uint32_t>(data.size()) - 1u;
            if (denseIdx != last)
            {
                data[denseIdx]            = data[last];
                modifiedFrame[denseIdx]   = modifiedFrame[last];
                const uint32_t movedEntity = dense_to_entity[last];
                dense_to_entity[denseIdx]  = movedEntity;
                sparse[movedEntity]        = denseIdx;
            }

            data.pop_back();
            dense_to_entity.pop_back();
            modifiedFrame.pop_back();
            sparse[entityIdx] = UINT32_MAX;
            --count;
            structurallyDirty = true;
        }

        // Read-only — never marks dirty.
        [[nodiscard]] const T* Get(uint32_t entityIdx) const noexcept
        {
            if (entityIdx >= sparse.size()) return nullptr;
            const uint32_t d = sparse[entityIdx];
            return d == UINT32_MAX ? nullptr : &data[d];
        }

        [[nodiscard]] T* GetRaw(uint32_t entityIdx) noexcept
        {
            if (entityIdx >= sparse.size()) return nullptr;
            const uint32_t d = sparse[entityIdx];
            return d == UINT32_MAX ? nullptr : &data[d];
        }

        // Returns a DirtyRef — destructor writes modifiedFrame on scope exit.
        // Invariant 8: only call from job threads with declared write access.
        [[nodiscard]] DirtyRef GetMutable(uint32_t entityIdx, uint32_t currentFrame) noexcept
        {
            ZERO_CORE_ASSERT(entityIdx < sparse.size(), "entityIdx out of sparse bounds");
            if (entityIdx < sparse.size())
            {
                const uint32_t d = sparse[entityIdx];
                ZERO_CORE_ASSERT(d != UINT32_MAX, "Entity does not have this component");
                if (d != UINT32_MAX && d < data.size())
                    return DirtyRef{ data[d], modifiedFrame[d], currentFrame };
            }

            // Release-build safety fallback — prevent OOB / data[UINT32_MAX] dereference.
            static thread_local uint32_t s_dummyFrame = 0;
            if constexpr (std::is_default_constructible_v<T>)
            {
                static thread_local T s_dummyValue{};
                return DirtyRef{ s_dummyValue, s_dummyFrame, currentFrame };
            }
            else
            {
                struct alignas(T) DummyStorage { unsigned char bytes[sizeof(T)]{}; };
                static thread_local DummyStorage s_dummyStorage{};
                return DirtyRef{ *reinterpret_cast<T*>(s_dummyStorage.bytes), s_dummyFrame, currentFrame };
            }
        }

        [[nodiscard]] bool Has(uint32_t entityIdx) const noexcept
        {
            return entityIdx < sparse.size() && sparse[entityIdx] != UINT32_MAX;
        }

        [[nodiscard]] uint32_t Count() const noexcept { return count; }

#ifdef ECS_VALIDATE
        void ValidateStructure() const
        {
            // Invariant 1
            ZERO_CORE_ASSERT(data.size() == dense_to_entity.size(),
                "Pool parallel array size mismatch (data vs dense_to_entity)");
            ZERO_CORE_ASSERT(data.size() == modifiedFrame.size(),
                "Pool parallel array size mismatch (data vs modifiedFrame)");
            ZERO_CORE_ASSERT(count == static_cast<uint32_t>(data.size()),
                "Pool count field out of sync with data.size()");

            // Invariant 2 & 3: sparse <-> dense round-trip (bijection guarantees no duplicates)
            for (uint32_t i = 0; i < static_cast<uint32_t>(data.size()); ++i)
            {
                const uint32_t eidx = dense_to_entity[i];
                ZERO_CORE_ASSERT(eidx < sparse.size(), "dense_to_entity entry out of sparse bounds");
                ZERO_CORE_ASSERT(sparse[eidx] == i, "sparse/dense mapping broken");
            }
        }
#endif
    };
}
