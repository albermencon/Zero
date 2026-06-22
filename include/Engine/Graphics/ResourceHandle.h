#pragma once
#include <cstdint>
#include <type_traits>
#include <Engine/Core.h>

namespace Zero
{
    // Lifecycle of a GPU resource as seen by client code.
    enum class ENGINE_API ResourceState : uint8_t
    {
        Invalid = 0,    // not created, or destroyed
        Pending,        // command enqueued, RenderThread hasn't processed it yet
        Ready,          // GPU resource exists, safe to reference in FrameData
        DestroyPending  // scheduled for destruction, do not use for new draws
    };

    // ResourceHandle<Tag>
    //   Opaque, trivially-copyable client handle to a GPU resource.
    //
    //   - Returned immediately by RenderInterface::Create*().
    //   - Safe to copy/share across threads.
    //   - State queries (IsReady) must be done through RenderInterface.
    //   - Never exposes internal RHI indices.
    template<typename Tag>
    struct ENGINE_API ResourceHandle
    {
        uint32_t value = 0; // 16-bit generation | 16-bit index

        [[nodiscard]] constexpr uint16_t GetIndex() const noexcept { return static_cast<uint16_t>(value & 0xFFFFu); }
        [[nodiscard]] constexpr uint16_t GetGeneration() const noexcept { return static_cast<uint16_t>((value >> 16) & 0xFFFFu); }
        [[nodiscard]] constexpr bool IsValid() const noexcept { return value != 0; }
        [[nodiscard]] constexpr uint32_t GetId() const noexcept { return value; }

        constexpr bool operator==(const ResourceHandle& o) const noexcept { return value == o.value; }
        constexpr bool operator!=(const ResourceHandle& o) const noexcept { return value != o.value; }
    };

    // One tag per resource kind — keeps handle types incompatible at compile time.
    struct BufferTag {};
    struct TextureTag {};
    struct PipelineTag {};

    using BufferHandle = ResourceHandle<BufferTag>;
    using TextureHandle = ResourceHandle<TextureTag>;
    using PipelineHandle = ResourceHandle<PipelineTag>;

    static_assert(std::is_trivially_copyable_v<BufferHandle>, "Handles must be trivially copyable.");
    static_assert(sizeof(BufferHandle) == 4, "Handles must be exactly 4 bytes.");
    static_assert(sizeof(TextureHandle) == 4, "Handles must be exactly 4 bytes.");
    static_assert(sizeof(PipelineHandle) == 4, "Handles must be exactly 4 bytes.");
}
