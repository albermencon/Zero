#pragma once
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory_resource>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>
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
    //   HOT  (iterated every frame)             : data[], dense_to_entity[]
    //   COLD (monotonic dirty + provenance)     : modifiedEpoch[], modifiedSource[]
    //   TOMBSTONES (drained before frame end)   : removedEntities[]
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

    // Snapshot of a component removed during FlushCommands() (via RemoveComponent
    // or DestroyEntity), captured BEFORE swap-and-pop overwrites the dense slot.
    // Lets handle-owning subsystems (RenderSyncSystem, PhysicsSyncSystem) release
    // external resources without leaking.
    template<typename T>
    struct RemovedComponent
    {
        Entity entity = Invalid;
        T      value;
    };

    template<typename T>
        requires std::is_trivially_copyable_v<T>
    struct ComponentPool : PoolBase
    {
        static constexpr uint32_t DefaultTombstoneReserve = 256;

        // HOT
        EcsVec<T>        data;
        EcsVec<uint32_t> dense_to_entity;

        // COLD — monotonic dirty tracking + write provenance
        // modifiedEpoch[i]  = monotonic registry epoch of the last write to dense slot i.
        // modifiedSource[i] = provenance of that write (Command / Gameplay / Physics / System).
        // Never stored inside T — preserves the hot struct's cache footprint.
        EcsVec<uint64_t>    modifiedEpoch;
        EcsVec<WriteSource> modifiedSource;

        // TOMBSTONES — populated during FlushCommands(), cleared at end-of-frame.
        EcsVec<RemovedComponent<T>> removedEntities;

        // STRUCTURE — read-only during the job window
        // sparse[entityIdx] = dense index, UINT32_MAX = not present
        EcsVec<uint32_t> sparse;

        // DirtyRef — RAII proxy returned by GetMutable().
        // Writes modifiedEpoch and modifiedSource once, on scope exit.
        // Call only from job threads with declared write access to this pool.
        struct DirtyRef
        {
            T&           value;
            uint64_t&    epochSlot;
            WriteSource& sourceSlot;
            uint64_t     writeEpoch;
            WriteSource  writeSource;

            DirtyRef(T& v, uint64_t& e, WriteSource& s, uint64_t we, WriteSource ws) noexcept
                : value(v)
                , epochSlot(e)
                , sourceSlot(s)
                , writeEpoch(we)
                , writeSource(ws)
            {}

            ~DirtyRef()
            {
                epochSlot  = writeEpoch;
                sourceSlot = writeSource;
            }

            DirtyRef(const DirtyRef&)            = delete;
            DirtyRef& operator=(const DirtyRef&) = delete;

            T* operator->() noexcept { return &value; }
            T& operator*()  noexcept { return value; }
        };

        // Bulk read-only span view.
        struct ConstBulkView
        {
            std::span<const T>           data;
            std::span<const uint32_t>    entities;
            std::span<const uint64_t>    epochs;
            std::span<const WriteSource> sources;

            [[nodiscard]] uint32_t Count() const noexcept
            {
                return static_cast<uint32_t>(data.size());
            }

            [[nodiscard]] bool IsDirtySince(uint32_t denseIdx, uint64_t lastSyncedEpoch) const noexcept
            {
                return epochs[denseIdx] > lastSyncedEpoch;
            }

            [[nodiscard]] bool IsDirtyFrom(uint32_t denseIdx,
                                           uint64_t lastSyncedEpoch,
                                           WriteSource sourceMask) const noexcept
            {
                return epochs[denseIdx] > lastSyncedEpoch &&
                       HasWriteSource(sources[denseIdx], sourceMask);
            }
        };

        // Bulk read/write span view — lets physics/gameplay sync passes perform
        // flat linear writes with explicit monotonic epoch + provenance stamping.
        struct MutableBulkView
        {
            std::span<T>              data;
            std::span<const uint32_t> entities;
            std::span<uint64_t>       epochs;
            std::span<WriteSource>    sources;
            uint64_t                  writeEpoch  = 0;
            WriteSource               writeSource = WriteSource::Gameplay;

            [[nodiscard]] uint32_t Count() const noexcept
            {
                return static_cast<uint32_t>(data.size());
            }

            [[nodiscard]] bool IsDirtySince(uint32_t denseIdx, uint64_t lastSyncedEpoch) const noexcept
            {
                return epochs[denseIdx] > lastSyncedEpoch;
            }

            [[nodiscard]] bool IsDirtyFrom(uint32_t denseIdx,
                                           uint64_t lastSyncedEpoch,
                                           WriteSource sourceMask) const noexcept
            {
                return epochs[denseIdx] > lastSyncedEpoch &&
                       HasWriteSource(sources[denseIdx], sourceMask);
            }

            void Write(uint32_t denseIdx, const T& value) noexcept
            {
                data[denseIdx]    = value;
                epochs[denseIdx]  = writeEpoch;
                sources[denseIdx] = writeSource;
            }

            void MarkDirty(uint32_t denseIdx) noexcept
            {
                epochs[denseIdx]  = writeEpoch;
                sources[denseIdx] = writeSource;
            }

            void MarkAllDirty() noexcept
            {
                std::fill(epochs.begin(), epochs.end(), writeEpoch);
                std::fill(sources.begin(), sources.end(), writeSource);
            }
        };

        // Reserve — call once at pool construction before any Insert.
#ifndef ECS_NO_PMR
        void Reserve(uint32_t entityCapacity, uint32_t componentCapacity,
                     std::pmr::memory_resource* mr = std::pmr::get_default_resource())
        {
            sparse          = EcsVec<uint32_t>(mr);
            data            = EcsVec<T>(mr);
            dense_to_entity = EcsVec<uint32_t>(mr);
            modifiedEpoch   = EcsVec<uint64_t>(mr);
            modifiedSource  = EcsVec<WriteSource>(mr);
            removedEntities = EcsVec<RemovedComponent<T>>(mr);

            sparse.assign(entityCapacity, UINT32_MAX);
            data.reserve(componentCapacity);
            dense_to_entity.reserve(componentCapacity);
            modifiedEpoch.reserve(componentCapacity);
            modifiedSource.reserve(componentCapacity);
            removedEntities.reserve(std::min(componentCapacity, DefaultTombstoneReserve));
        }
#else
        void Reserve(uint32_t entityCapacity, uint32_t componentCapacity)
        {
            sparse.assign(entityCapacity, UINT32_MAX);
            data.reserve(componentCapacity);
            dense_to_entity.reserve(componentCapacity);
            modifiedEpoch.reserve(componentCapacity);
            modifiedSource.reserve(componentCapacity);
            removedEntities.reserve(std::min(componentCapacity, DefaultTombstoneReserve));
        }
#endif

        // Insert — main thread only, inside FlushCommands().
        // All dense arrays grow atomically.
        // Entity must not already be present.
        void Insert(uint32_t entityIdx,
                    T value,
                    uint64_t epoch,
                    WriteSource source = WriteSource::Command)
        {
            ZERO_CORE_ASSERT(entityIdx < sparse.size(), "entityIdx out of sparse bounds");
            ZERO_CORE_ASSERT(sparse[entityIdx] == UINT32_MAX, "Entity already has this component");

            sparse[entityIdx] = static_cast<uint32_t>(data.size());
            dense_to_entity.push_back(entityIdx);
            data.push_back(std::move(value));
            modifiedEpoch.push_back(epoch);
            modifiedSource.push_back(source);
            ++count;
            structurallyDirty = true;
        }

        // Remove — main thread only, inside FlushCommands().
        // Records a typed tombstone (entity handle + component snapshot) BEFORE
        // O(1) swap-and-pop removes the element from the dense arrays.
        void Remove(Entity entity)
        {
            const uint32_t entityIdx = Index(entity);
            ZERO_CORE_ASSERT(entityIdx < sparse.size(), "entityIdx out of sparse bounds");
            const uint32_t denseIdx = sparse[entityIdx];
            if (denseIdx == UINT32_MAX) return;

            // Populate tombstone before swap-and-pop overwrites data[denseIdx].
            removedEntities.push_back(RemovedComponent<T>{ entity, data[denseIdx] });
            hasTombstones = true;

            const uint32_t last = static_cast<uint32_t>(data.size()) - 1u;
            if (denseIdx != last)
            {
                data[denseIdx]            = data[last];
                modifiedEpoch[denseIdx]   = modifiedEpoch[last];
                modifiedSource[denseIdx]  = modifiedSource[last];
                const uint32_t movedEntity = dense_to_entity[last];
                dense_to_entity[denseIdx]  = movedEntity;
                sparse[movedEntity]        = denseIdx;
            }

            data.pop_back();
            dense_to_entity.pop_back();
            modifiedEpoch.pop_back();
            modifiedSource.pop_back();
            sparse[entityIdx] = UINT32_MAX;
            --count;
            structurallyDirty = true;
        }

        // Convenience overload for callers holding only an entity index.
        void Remove(uint32_t entityIdx)
        {
            Remove(Make(entityIdx, 0));
        }

        // Clears accumulated tombstones while preserving reserved capacity
        void ClearTombstones() noexcept
        {
            removedEntities.clear();
            hasTombstones = false;
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
#ifdef ZERO_ENABLE_ASSERTS
            if (componentId < 128u)
            {
                if (const Detail::SystemAccessView* access = Detail::GetActiveSystemAccess())
                {
                    ZERO_CORE_ASSERT(access->CanWrite(componentId),
                        "ECS SystemAccess violation: GetRaw() called without declaring T in writes");
                }
            }
#endif
            if (entityIdx >= sparse.size()) return nullptr;
            const uint32_t d = sparse[entityIdx];
            return d == UINT32_MAX ? nullptr : &data[d];
        }

        // Returns a DirtyRef — destructor stamps modifiedEpoch and modifiedSource
        // on scope exit. Only call with declared write access.
        [[nodiscard]] DirtyRef GetMutable(uint32_t entityIdx,
                                          uint64_t epoch,
                                          WriteSource source = WriteSource::Gameplay) noexcept
        {
#ifdef ZERO_ENABLE_ASSERTS
            if (componentId < 128u)
            {
                if (const Detail::SystemAccessView* access = Detail::GetActiveSystemAccess())
                {
                    ZERO_CORE_ASSERT(access->CanWrite(componentId),
                        "ECS SystemAccess violation: GetMutable() called without declaring T in writes");
                }
            }
#endif
            ZERO_CORE_ASSERT(entityIdx < sparse.size(), "entityIdx out of sparse bounds");
            if (entityIdx < sparse.size())
            {
                const uint32_t d = sparse[entityIdx];
                ZERO_CORE_ASSERT(d != UINT32_MAX, "Entity does not have this component");
                if (d != UINT32_MAX && d < data.size())
                    return DirtyRef{ data[d], modifiedEpoch[d], modifiedSource[d], epoch, source };
            }

            // Release-build safety fallback — prevent OOB / data[UINT32_MAX] dereference.
            static thread_local uint64_t    s_dummyEpoch  = 0;
            static thread_local WriteSource s_dummySource = WriteSource::None;
            if constexpr (std::is_default_constructible_v<T>)
            {
                static thread_local T s_dummyValue{};
                return DirtyRef{ s_dummyValue, s_dummyEpoch, s_dummySource, epoch, source };
            }
            else
            {
                struct alignas(T) DummyStorage { unsigned char bytes[sizeof(T)]{}; };
                static thread_local DummyStorage s_dummyStorage{};
                return DirtyRef{ *reinterpret_cast<T*>(s_dummyStorage.bytes), s_dummyEpoch, s_dummySource, epoch, source };
            }
        }

        void MarkDirty(uint32_t entityIdx, uint64_t epoch, WriteSource source = WriteSource::Gameplay) noexcept
        {
#ifdef ZERO_ENABLE_ASSERTS
            if (componentId < 128u)
            {
                if (const Detail::SystemAccessView* access = Detail::GetActiveSystemAccess())
                {
                    ZERO_CORE_ASSERT(access->CanWrite(componentId),
                        "ECS SystemAccess violation: MarkDirty() called without declaring T in writes");
                }
            }
#endif
            if (entityIdx >= sparse.size()) return;
            const uint32_t d = sparse[entityIdx];
            if (d == UINT32_MAX || d >= data.size()) return;
            modifiedEpoch[d]  = epoch;
            modifiedSource[d] = source;
        }

        [[nodiscard]] uint64_t ModifiedEpochOf(uint32_t entityIdx) const noexcept
        {
            if (entityIdx >= sparse.size()) return 0;
            const uint32_t d = sparse[entityIdx];
            return (d == UINT32_MAX || d >= modifiedEpoch.size()) ? 0 : modifiedEpoch[d];
        }

        [[nodiscard]] WriteSource ModifiedSourceOf(uint32_t entityIdx) const noexcept
        {
            if (entityIdx >= sparse.size()) return WriteSource::None;
            const uint32_t d = sparse[entityIdx];
            return (d == UINT32_MAX || d >= modifiedSource.size()) ? WriteSource::None : modifiedSource[d];
        }

        [[nodiscard]] bool IsDirtySince(uint32_t entityIdx,
                                        uint64_t lastSyncedEpoch,
                                        WriteSource sourceMask = WriteSource::Any) const noexcept
        {
            if (entityIdx >= sparse.size()) return false;
            const uint32_t d = sparse[entityIdx];
            if (d == UINT32_MAX || d >= modifiedEpoch.size()) return false;
            return modifiedEpoch[d] > lastSyncedEpoch &&
                   HasWriteSource(modifiedSource[d], sourceMask);
        }

        // Bulk-span accessors — preferred subsystem boundary API.
        [[nodiscard]] ConstBulkView ReadView() const noexcept
        {
            return ConstBulkView{
                std::span<const T>{ data.data(), data.size() },
                std::span<const uint32_t>{ dense_to_entity.data(), dense_to_entity.size() },
                std::span<const uint64_t>{ modifiedEpoch.data(), modifiedEpoch.size() },
                std::span<const WriteSource>{ modifiedSource.data(), modifiedSource.size() }
            };
        }

        [[nodiscard]] MutableBulkView WriteView(uint64_t epoch,
                                                WriteSource source = WriteSource::Gameplay) noexcept
        {
#ifdef ZERO_ENABLE_ASSERTS
            if (componentId < 128u)
            {
                if (const Detail::SystemAccessView* access = Detail::GetActiveSystemAccess())
                {
                    ZERO_CORE_ASSERT(access->CanWrite(componentId),
                        "ECS SystemAccess violation: WriteView() called without declaring T in writes");
                }
            }
#endif
            return MutableBulkView{
                std::span<T>{ data.data(), data.size() },
                std::span<const uint32_t>{ dense_to_entity.data(), dense_to_entity.size() },
                std::span<uint64_t>{ modifiedEpoch.data(), modifiedEpoch.size() },
                std::span<WriteSource>{ modifiedSource.data(), modifiedSource.size() },
                epoch,
                source
            };
        }

        [[nodiscard]] std::span<const T> DataSpan() const noexcept
        {
            return { data.data(), data.size() };
        }

        [[nodiscard]] std::span<T> MutableDataSpan() noexcept
        {
#ifdef ZERO_ENABLE_ASSERTS
            if (componentId < 128u)
            {
                if (const Detail::SystemAccessView* access = Detail::GetActiveSystemAccess())
                {
                    ZERO_CORE_ASSERT(access->CanWrite(componentId),
                        "ECS SystemAccess violation: MutableDataSpan() called without declaring T in writes");
                }
            }
#endif
            return { data.data(), data.size() };
        }

        [[nodiscard]] std::span<const uint32_t> EntityIndices() const noexcept
        {
            return { dense_to_entity.data(), dense_to_entity.size() };
        }

        [[nodiscard]] std::span<const uint64_t> ModifiedEpochs() const noexcept
        {
            return { modifiedEpoch.data(), modifiedEpoch.size() };
        }

        [[nodiscard]] std::span<const WriteSource> ModifiedSources() const noexcept
        {
            return { modifiedSource.data(), modifiedSource.size() };
        }

        // Read-only span of component removals accumulated since the last ClearTombstones().
        [[nodiscard]] std::span<const RemovedComponent<T>> Removed() const noexcept
        {
            return { removedEntities.data(), removedEntities.size() };
        }

        [[nodiscard]] bool Has(uint32_t entityIdx) const noexcept
        {
            return entityIdx < sparse.size() && sparse[entityIdx] != UINT32_MAX;
        }

        [[nodiscard]] uint32_t Count() const noexcept { return count; }

#ifdef ECS_VALIDATE
        void ValidateStructure() const
        {
            ZERO_CORE_ASSERT(data.size() == dense_to_entity.size(),
                "Pool parallel array size mismatch (data vs dense_to_entity)");
            ZERO_CORE_ASSERT(data.size() == modifiedEpoch.size(),
                "Pool parallel array size mismatch (data vs modifiedEpoch)");
            ZERO_CORE_ASSERT(data.size() == modifiedSource.size(),
                "Pool parallel array size mismatch (data vs modifiedSource)");
            ZERO_CORE_ASSERT(count == static_cast<uint32_t>(data.size()),
                "Pool count field out of sync with data.size()");

            // Sparse <-> dense round-trip (bijection guarantees no duplicates)
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
