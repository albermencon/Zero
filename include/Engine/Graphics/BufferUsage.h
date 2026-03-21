#pragma once
#include <cstdint>
#include <Engine/Core.h>

namespace Zero
{
    // Bitmask — declare intended usage at creation time.
    enum class ENGINE_API BufferUsage : uint32_t
    {
        None = 0,
        Vertex = 1 << 0,
        Index = 1 << 1,
        Uniform = 1 << 2,       // UBO / CBV
        Storage = 1 << 3,       // SSBO / UAV
        Indirect = 1 << 4,      // indirect draw/dispatch args
        TransferSrc = 1 << 5,   // staging source
        TransferDst = 1 << 6    // GPU-side destination
    };

    // Bitmask operators
    inline constexpr BufferUsage operator|(BufferUsage a, BufferUsage b)
    {
        return static_cast<BufferUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    inline constexpr BufferUsage operator&(BufferUsage a, BufferUsage b)
    {
        return static_cast<BufferUsage>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
    }

    inline constexpr BufferUsage& operator|=(BufferUsage& a, BufferUsage b)
    {
        a = a | b; return a;
    }

    inline constexpr bool hasUsage(BufferUsage flags, BufferUsage query)
    {
        return (flags & query) != BufferUsage::None;
    }
}
