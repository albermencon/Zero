#pragma once
#include <cstdint>
#include <Engine/Core.h>
#include <Engine/Log.h>

namespace Zero 
{
    struct ComponentMask
    {
        uint64_t lo = 0;
        uint64_t hi = 0;

        constexpr void Set(uint32_t id) noexcept 
        {
            ZERO_ASSERT(id < 128, "Component ID out of range");
            if (id < 64) lo |=  (1ULL << id);
            else         hi |=  (1ULL << (id - 64));
        }

        constexpr void Clear(uint32_t id) noexcept 
        {
            ZERO_ASSERT(id < 128, "Component ID out of range");
            if (id < 64) lo &= ~(1ULL << id);
            else         hi &= ~(1ULL << (id - 64));
        }

        [[nodiscard]] constexpr bool Has(uint32_t id) const noexcept 
        {
            ZERO_ASSERT(id < 128, "Component ID out of range");
            if (id < 64) return (lo >> id) & 1ULL;
            else         return (hi >> (id - 64)) & 1ULL;
        }

        [[nodiscard]] constexpr bool Contains(const ComponentMask& req) const noexcept 
        {
            return ((lo & req.lo) == req.lo) && ((hi & req.hi) == req.hi);
        }

        [[nodiscard]] constexpr bool ContainsAny(const ComponentMask& req) const noexcept 
        {
            return (lo & req.lo) || (hi & req.hi);
        }

        [[nodiscard]] constexpr bool IsEmpty() const noexcept 
        {
            return lo == 0 && hi == 0;
        }
    };
}
