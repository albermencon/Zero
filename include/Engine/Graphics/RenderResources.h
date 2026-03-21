#pragma once
#include <cstdint>
#include <Engine/Core.h>
#include <Engine/Graphics/GraphicsTypes.h>   // PixelFormat, SampleCount, etc.
#include <Engine/Graphics/BufferUsage.h>
#include <Engine/Graphics/MemoryDomain.h>
#include <Engine/Graphics/VertexLayout.h>

namespace Zero
{
    // BufferDesc
    //   Describes a GPU buffer to allocate.
    //   initialData / initialDataSize: optional immediate upload on creation.
    //   The data pointer must remain valid until the MainThread drains the
    //   pending upload queue (i.e. within the same frame the call is made).
    struct ENGINE_API BufferDesc
    {
        const char* debugName = nullptr;
        uint64_t     size = 0;
        BufferUsage  usage = BufferUsage::None;
        MemoryDomain memory = MemoryDomain::GPU;
        const void* initialData = nullptr;
        uint64_t     initialDataSize = 0;   // must be <= size if set

        // --- Factories -------------------------------------------------------

        [[nodiscard]] static constexpr BufferDesc Vertex(uint64_t bytes, const char* name = nullptr)
        {
            return { name, bytes, BufferUsage::Vertex | BufferUsage::TransferDst, MemoryDomain::GPU };
        }

        [[nodiscard]] static constexpr BufferDesc Index(uint64_t bytes, const char* name = nullptr)
        {
            return { name, bytes, BufferUsage::Index | BufferUsage::TransferDst, MemoryDomain::GPU };
        }

        [[nodiscard]] static constexpr BufferDesc Uniform(uint64_t bytes, const char* name = nullptr)
        {
            return { name, bytes, BufferUsage::Uniform | BufferUsage::TransferDst, MemoryDomain::CPUtoGPU };
        }

        [[nodiscard]] static constexpr BufferDesc Storage(uint64_t bytes, const char* name = nullptr)
        {
            return { name, bytes, BufferUsage::Storage | BufferUsage::TransferDst, MemoryDomain::GPU };
        }

        [[nodiscard]] static constexpr BufferDesc Staging(uint64_t bytes, const char* name = nullptr)
        {
            return { name, bytes, BufferUsage::TransferSrc, MemoryDomain::CPUtoGPU };
        }
    };

    // MeshDesc
    //   Convenience: upload vertex + index data in a single call.
    //   RenderInterface::CreateMesh() allocates both buffers and enqueues
    //   their UploadRequests. Returns a MeshHandle (pair of BufferHandles).
    struct ENGINE_API MeshDesc
    {
        const char* debugName = nullptr;

        const void* vertexData = nullptr;
        uint64_t     vertexBytes = 0;
        VertexLayout vertexLayout;              // stride / attributes

        const void* indexData = nullptr;
        uint64_t     indexBytes = 0;
        bool         use32BitIndex = true;     // false = uint16_t indices

        uint32_t     vertexCount = 0;
        uint32_t     indexCount = 0;
    };

    // TextureDesc  (alias — ImageDesc is already the full public descriptor)
    //   Defined in include/Engine/Graphics/ImageDesc.h.
    //   TextureUpdate describes a CPU→GPU upload for one mip/layer slice.
    struct ENGINE_API TextureUpdate
    {
        const void* data = nullptr;
        uint64_t    dataSize = 0;     // bytes
        uint32_t    mipLevel = 0;
        uint32_t    arrayLayer = 0;
        uint32_t    width = 0;
        uint32_t    height = 0;
    };

    // BufferUpdate
    //   Partial or full CPU→GPU buffer write from any thread.
    //   data pointer must remain valid until MainThread drains pending uploads
    //   (same frame). For cross-frame async updates, use a staging buffer.
    struct ENGINE_API BufferUpdate
    {
        const void* data = nullptr;
        uint64_t    size = 0;
        uint64_t    dstOffset = 0;  // byte offset into destination buffer
    };

    // MeshHandle
    //   Lightweight pair returned by RenderInterface::CreateMesh().
    //   Both handles become Ready together (same frame).
    struct ENGINE_API MeshHandle
    {
        BufferHandle vertexBuffer;
        BufferHandle indexBuffer;

        [[nodiscard]] bool IsReady()   const noexcept { return vertexBuffer.IsReady() && indexBuffer.IsReady(); }
        [[nodiscard]] bool IsPending() const noexcept { return vertexBuffer.IsPending() || indexBuffer.IsPending(); }
        [[nodiscard]] bool IsValid()   const noexcept { return vertexBuffer.IsValid() && indexBuffer.IsValid(); }
    };
}
