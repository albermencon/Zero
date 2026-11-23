#pragma once
#include <cstdint>
#include <atomic>
#include <cassert>
#include <concurrentqueue.h>
#include <Engine/Core.h>
#include <Engine/Log.h>
#include <Engine/Ecs/Entity.h>

namespace Zero
{
    // MPMC-safe entity handle factory.
    //   - MintEntity()      : any thread — ConcurrentQueue free list + atomic index
    //   - FinalizeDestroy() : main thread only, called inside FlushCommands()
    //   - IsAlive()         : any thread — acquire-load of generation
    //
    // Slot 0 is permanently reserved as Invalid.
    // Generation wraps at 2^12 (4096) — stale handles are detectable for ~4096 reuses.

    class EntityAllocator
    {
    public:
        static constexpr uint32_t MaxEntities = 1u << 20;  // ~1M live entities

        EntityAllocator()
        {
            // Slot 0 is permanently invalid — start fresh from index 1.
            m_nextIndex.store(1, std::memory_order_relaxed);
        }

        // MPMC-safe. Returns a fully live handle — IsAlive() is true immediately.
        // For recycled slots, generation was bumped before the index re-entered the list.
        // For fresh slots, generation table is zero-initialized at startup.
        [[nodiscard]] Entity MintEntity() noexcept
        {
            uint32_t idx = 0;
            if (!m_freeList.try_dequeue(idx))
            {
                uint32_t cur = m_nextIndex.load(std::memory_order_relaxed);
                for (;;)
                {
                    if (cur >= MaxEntities)
                    {
                        ZERO_CORE_ASSERT(false, "Entity index space exhausted");
                        return Invalid;
                    }
                    if (m_nextIndex.compare_exchange_weak(cur, cur + 1u,
                            std::memory_order_relaxed, std::memory_order_relaxed))
                    {
                        idx = cur;
                        break;
                    }
                }
            }

            ZERO_CORE_ASSERT(idx > 0 && idx < MaxEntities, "Entity index space exhausted");
            if (idx == 0 || idx >= MaxEntities)
                return Invalid;

            const uint32_t gen = m_generations[idx].load(std::memory_order_acquire);
            return Make(idx, gen);
        }

        // Main thread only — called during FlushCommands().
        // Invariant 6: generation only ever increases.
        // Bump happens BEFORE the index re-enters the free list, so any new handle
        // minted after this call will read the incremented generation.
        //
        // Slot retirement: when a slot's generation reaches the maximum value
        // representable in the entity handle, the slot is permanently retired
        // (marked with a dead-forever sentinel and never recycled). This
        // prevents the 12-bit generation field from wrapping and re-aliasing a
        // stale handle onto a live entity. ~1M slots x 4096 lifetimes is
        // effectively unbounded.
        void FinalizeDestroy(uint32_t idx) noexcept
        {
            ZERO_CORE_ASSERT(idx > 0 && idx < MaxEntities, "Entity index out of range");
            if (idx == 0 || idx >= MaxEntities) return;
            constexpr uint32_t GenMax  = (1u << GenBits) - 1u; // 4095
            constexpr uint32_t Retired = UINT32_MAX;           // dead-forever sentinel
            const uint32_t gen = m_generations[idx].load(std::memory_order_acquire);
            if (gen >= GenMax)
            {
                m_generations[idx].store(Retired, std::memory_order_release);
                return; // permanently retired — never recycle this slot
            }
            m_generations[idx].store(gen + 1u, std::memory_order_release);
            m_freeList.enqueue(idx);
        }

        // Any thread. Compares live generation vs handle generation.
        [[nodiscard]] bool IsAlive(Entity e) const noexcept
        {
            const uint32_t idx = Index(e);
            if (idx == 0 || idx >= MaxEntities) return false;
            return m_generations[idx].load(std::memory_order_acquire)
                   == Generation(e);
        }

        // Any thread. Returns the current live generation for a slot index.
        [[nodiscard]] uint32_t CurrentGeneration(uint32_t idx) const noexcept
        {
            ZERO_CORE_ASSERT(idx > 0 && idx < MaxEntities, "Entity index out of range");
            if (idx == 0 || idx >= MaxEntities) return 0;
            return m_generations[idx].load(std::memory_order_acquire);
        }

    private:
        // Generation table. Zero-initialized — every slot starts at gen 0.
        // Using uint32_t per slot (not uint16_t) to avoid narrowing when embedded
        // in a 64-bit Entity via Make(). The 12-bit mask in GenMask handles truncation.
        std::atomic<uint32_t>             m_generations[MaxEntities]{};
        std::atomic<uint32_t>             m_nextIndex{ 1 };
        moodycamel::ConcurrentQueue<uint32_t> m_freeList;
    };
}
