#pragma once
#include <cstring>
#include <memory>
#include <new>
#include <type_traits>
#include <Engine/Export.h>
#include <Engine/Ecs/EcsCommand.h>
#include <Engine/Ecs/FrameSlab.h>
#include <Engine/Ecs/ComponentTrait.h>
#include <Engine/Ecs/Entity.h>

namespace Zero
{
    // Bundles the MPMC command queue with the FrameSlab for oversized payloads.
    // Both live together so FlushCommands() takes a single argument.
    //
    // Thread safety:
    //   EnqueueAdd / EnqueueSet / EnqueueRemove / EnqueueDestroy — any thread
    //   TryDequeue / slab reads / slab.Reset()                   — main thread only (Invariant 9)
    struct ZERO_API EcsCommandQueue
    {
        FrameSlab slab;

        explicit EcsCommandQueue(uint32_t slabBytes = 4u * 1024u * 1024u);
        ~EcsCommandQueue();

        EcsCommandQueue(const EcsCommandQueue&)            = delete;
        EcsCommandQueue& operator=(const EcsCommandQueue&) = delete;
        EcsCommandQueue(EcsCommandQueue&&)                 = delete;
        EcsCommandQueue& operator=(EcsCommandQueue&&)      = delete;

        // Store a component command. Inline if sizeof(T) <= 44, slab otherwise
        // (with aligned heap fallback if the slab is exhausted).
        template<typename T>
        void EnqueueAdd(Entity entity, T value)
        {
            EnqueueTyped(EcsCommandType::AddComponent, entity, value);
        }

        // SetComponent — overwrites an existing component or inserts if absent.
        template<typename T>
        void EnqueueSet(Entity entity, T value)
        {
            EnqueueTyped(EcsCommandType::SetComponent, entity, value);
        }

        void EnqueueRemove(Entity entity, uint8_t componentId);
        void EnqueueDestroy(Entity entity);

        // Main-thread consumer API used by EcsRegistry::FlushCommands().
        [[nodiscard]] bool TryDequeue(EcsCommand& outCmd);

    private:
        void EnqueueCommand(const EcsCommand& cmd);

        template<typename T>
        void EnqueueTyped(EcsCommandType type, Entity entity, T value)
        {
            static_assert(std::is_trivially_copyable_v<T>,
                "ECS components must be trivially copyable");
            constexpr uint32_t id = ComponentId<T>();
            static_assert(id < 128, "Component ID out of range");

            EcsCommand cmd{};
            cmd.type        = type;
            cmd.componentId = static_cast<uint8_t>(id);
            cmd.entity      = entity;

            if constexpr (sizeof(T) <= sizeof(cmd.inlineData))
            {
                cmd.flags = 0;
                std::memcpy(cmd.inlineData, &value, sizeof(T));
            }
            else
            {
                const uint32_t off = slab.Store(value);
                if (off != UINT32_MAX)
                {
                    cmd.flags      = kEcsFlagSlabPayload;
                    cmd.slabOffset = off;
                    cmd.slabSize   = static_cast<uint32_t>(sizeof(T));
                    cmd.slabAlign  = static_cast<uint32_t>(alignof(T));
                }
                else
                {
                    // Slab exhausted — fall back to a heap-allocated payload.
                    void* mem = ::operator new(sizeof(T), std::align_val_t{ alignof(T) });
                    std::memcpy(mem, &value, sizeof(T));
                    cmd.flags      = kEcsFlagHeapPayload;
                    cmd.heapPtr    = mem;
                    cmd.heapSize   = static_cast<uint32_t>(sizeof(T));
                    cmd.heapAlign  = static_cast<uint32_t>(alignof(T));
                }
            }

            EnqueueCommand(cmd);
        }

        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}
