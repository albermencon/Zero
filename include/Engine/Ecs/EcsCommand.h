#pragma once
#include <cstdint>
#include <cassert>
#include <new>
#include "Entity.h"

namespace Zero
{
    // EcsCommand — exactly one cache line (64 bytes).
    //
    // Inline payload: components <= 44 bytes are stored directly.
    // Oversized components spill into FrameSlab; the command stores an offset.
    // If the slab is exhausted the payload is heap-allocated (kEcsFlagHeapPayload).
    //
    // componentId is always the compile-time-stable ID from ComponentTrait<T>::kId.

    enum class EcsCommandType : uint8_t
    {
        DestroyEntity,
        AddComponent,
        RemoveComponent,
        SetComponent,
    };

    inline constexpr uint16_t kEcsFlagSlabPayload = 0x0001;
    inline constexpr uint16_t kEcsFlagHeapPayload = 0x0002;

    struct alignas(64) EcsCommand
    {
        EcsCommandType type;
        uint8_t        componentId;    // ComponentTrait<T>::kId — always in [0, 127]
        uint16_t       flags;          // kEcsFlagSlabPayload / kEcsFlagHeapPayload
        uint32_t       _pad;           // explicit padding — no silent struct holes
        Entity         entity;         // generation embedded at MintEntity() time

        union
        {
            uint8_t inlineData[44];   // components whose sizeof(T) <= 44 stored here
            struct
            {
                uint32_t slabOffset;  // byte offset into FrameSlab buffer
                uint32_t slabSize;    // sizeof(T)
                uint32_t slabAlign;   // alignof(T) — required for safe As<T>() call
                uint8_t  _slabPad[32];
            };
            struct
            {
                const void* heapPtr;  // heap payload pointer when slab is full
                uint32_t    heapSize;
                uint32_t    heapAlign;
                uint8_t     _heapPad[28];
            };
        };
    };

    static_assert(sizeof(EcsCommand) == 64, "EcsCommand must be exactly one cache line (64 bytes)");
    static_assert(alignof(EcsCommand) == 64, "EcsCommand must be 64-byte aligned");
}
