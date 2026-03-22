#pragma once
#include <cstdint>
#include <atomic>
#include <Engine/Core.h>

namespace Zero
{
    // Lifecycle of a GPU resource as seen by client code.
    enum class ENGINE_API ResourceState : uint8_t
    {
        Invalid = 0,  // not created, or destroyed
        Pending,      // command enqueued, RenderThread hasn't processed it yet
        Ready,        // GPU resource exists, safe to reference in FrameData
    };

    // ResourceHandle<Tag>
    //   Opaque, ref-counted client handle to a GPU resource.
    //
    //   - Returned immediately by RenderInterface::Create*().
    //   - State starts as Pending; RenderThread sets it to Ready once the
    //     backing GPU object is created.
    //   - Safe to copy/share across threads — state reads are atomic.
    //   - The numeric ID is stable for the resource lifetime (logging, maps).
    //   - Never exposes internal RHI indices — only RenderInterface internals
    //     resolve those.
    template<typename Tag>
    class ENGINE_API ResourceHandle
    {
    public:
        ResourceHandle() = default;

        // Copy — copies the id and loads the state atomically
        ResourceHandle(const ResourceHandle& o) noexcept
            : m_id(o.m_id)
        {
            m_state.store(o.m_state.load(std::memory_order_acquire),
                std::memory_order_release);
        }

        ResourceHandle& operator=(const ResourceHandle& o) noexcept
        {
            if (this != &o)
            {
                m_id = o.m_id;
                m_state.store(o.m_state.load(std::memory_order_acquire),
                    std::memory_order_release);
            }
            return *this;
        }

        ResourceHandle(ResourceHandle&&) noexcept = default;
        ResourceHandle& operator=(ResourceHandle&&) noexcept = default;

        [[nodiscard]] bool IsValid()   const noexcept { return m_state.load(std::memory_order_acquire) != ResourceState::Invalid; }
        [[nodiscard]] bool IsReady()   const noexcept { return m_state.load(std::memory_order_acquire) == ResourceState::Ready; }
        [[nodiscard]] bool IsPending() const noexcept { return m_state.load(std::memory_order_acquire) == ResourceState::Pending; }
        [[nodiscard]] ResourceState GetState() const noexcept { return m_state.load(std::memory_order_acquire); }

        // Stable opaque ID — suitable as map key or for debug logging.
        [[nodiscard]] uint32_t GetId() const noexcept { return m_id; }

        bool operator==(const ResourceHandle& o) const noexcept { return m_id == o.m_id; }
        bool operator!=(const ResourceHandle& o) const noexcept { return m_id != o.m_id; }

    private:
        friend class RenderInterface;

        explicit ResourceHandle(uint32_t id) : m_id(id)
        {
            m_state.store(ResourceState::Pending, std::memory_order_release);
        }

        // Called by RenderThread once backing resource is materialised/destroyed.
        void MarkReady()   const noexcept { m_state.store(ResourceState::Ready, std::memory_order_release); }
        void MarkInvalid() const noexcept { m_state.store(ResourceState::Invalid, std::memory_order_release); }

        uint32_t m_id = 0;
        mutable std::atomic<ResourceState> m_state{ ResourceState::Invalid };
    };

    // One tag per resource kind — keeps handle types incompatible at compile time.
    struct BufferTag {};
    struct TextureTag {};
    struct PipelineTag {};

    using BufferHandle = ResourceHandle<BufferTag>;
    using TextureHandle = ResourceHandle<TextureTag>;
    using PipelineHandle = ResourceHandle<PipelineTag>;
}
