#pragma once
#include <Engine/Core.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "JobCounter.h"

namespace Zero
{
    // Tagged representation combining JobCounter* address and Mode bit.
    // The lower three bits are reserved by the Job ABI. 
    // Only bit 0 is currently used; bits 1-2 are reserved for future expansion.
    struct ZERO_API Job
    {
        using JobFn = void(*)(void*);

        enum class Mode : uint8_t
        {
            External = 0,
            Inline = 1
        };

        static constexpr uintptr_t TAG_MASK  = 0x7;
        static constexpr uintptr_t MODE_MASK = 0x1;

        JobFn fn{ nullptr };
        uintptr_t taggedCounter{ 0 };

        union
        {
            void* ptr;
            std::byte payload[16];
        };

        [[nodiscard]] ZERO_FORCE_INLINE JobCounter* GetCounter() const noexcept
        {
            return reinterpret_cast<JobCounter*>(taggedCounter & ~TAG_MASK);
        }

        ZERO_FORCE_INLINE void SetCounter(JobCounter* c) noexcept
        {
            const uintptr_t cleanPtr = Encode(c) & ~TAG_MASK;
            taggedCounter = cleanPtr | (taggedCounter & TAG_MASK);
        }

        [[nodiscard]] ZERO_FORCE_INLINE bool HasCounter() const noexcept
        {
            return (taggedCounter & ~TAG_MASK) != 0;
        }

        [[nodiscard]] ZERO_FORCE_INLINE Mode GetMode() const noexcept
        {
            return static_cast<Mode>(taggedCounter & MODE_MASK);
        }

        ZERO_FORCE_INLINE void SetMode(Mode mode) noexcept
        {
            taggedCounter = (taggedCounter & ~MODE_MASK) | (static_cast<uintptr_t>(mode) & MODE_MASK);
        }

        [[nodiscard]] ZERO_FORCE_INLINE bool IsInline() const noexcept
        {
            return GetMode() == Mode::Inline;
        }

        [[nodiscard]] ZERO_FORCE_INLINE bool IsExternal() const noexcept
        {
            return GetMode() == Mode::External;
        }

    private:
        [[nodiscard]] static ZERO_FORCE_INLINE uintptr_t Encode(JobCounter* ptr) noexcept
        {
            return reinterpret_cast<uintptr_t>(ptr);
        }
    };

    static_assert(sizeof(void*) == sizeof(uintptr_t), "Pointer size must match uintptr_t");
    static_assert(alignof(JobCounter) >= 8, "JobCounter must be at least 8-byte aligned for tagged pointers");
    static_assert(sizeof(Job) == 32, "Job size must be 32 bytes");
}
