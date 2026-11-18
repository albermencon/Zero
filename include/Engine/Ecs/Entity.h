#pragma once
#include <cstdint>

namespace Zero 
{
    using Entity = uint64_t;

    static constexpr uint64_t IndexBits = 20;
    static constexpr uint64_t GenBits   = 12;
    static constexpr uint64_t IndexMask = (1ULL << IndexBits) - 1;
    static constexpr uint64_t GenMask   = ((1ULL << GenBits) - 1) << IndexBits;
    static constexpr Entity   Invalid   = ~Entity{0};

    [[nodiscard]] inline constexpr uint32_t Index(Entity e) noexcept 
    {
        return static_cast<uint32_t>(e & IndexMask);
    }

    [[nodiscard]] inline constexpr uint32_t Generation(Entity e) noexcept 
    {
        return static_cast<uint32_t>((e & GenMask) >> IndexBits);
    }

    [[nodiscard]] inline constexpr Entity Make(uint32_t idx, uint32_t gen) noexcept 
    {
        return (static_cast<uint64_t>(gen) << IndexBits) | idx;
    }
}
