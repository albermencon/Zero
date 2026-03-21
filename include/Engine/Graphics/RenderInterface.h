#pragma once
#include <Engine/Core.h>
#include <Engine/Graphics/ResourceHandle.h>
#include <Engine/Graphics/RenderResources.h>
#include <Engine/Graphics/ImageDesc.h>
#include <Engine/Graphics/PipelineDesc.h>

namespace Zero
{
    // RenderInterface
    //
    //   Single public entry point for all GPU resource operations.
    //   Call from any thread (MainThread, WorkerThreads).
    //
    //   CREATION — deferred, non-blocking:
    //     Returns a Handle immediately (state = Pending).
    //     RenderThread materialises the resource on its next iteration.
    //     Safe to store in FrameData from the NEXT frame onwards
    //     (use IsReady() to guard first use).
    //
    //   DESTRUCTION — deferred:
    //     Handle is marked Invalid immediately.
    //     RenderThread destroys the backing resource once it is no longer
    //     in-flight (after MAX_FRAMES_IN_FLIGHT frames).
    //
    //   UPLOAD — enqueued, same-frame drain:
    //     UpdateBuffer / UpdateTexture enqueue an UploadRequest.
    //     The MainThread drains all pending uploads into FrameData during
    //     OnBuildFrame. Data pointers must remain valid until that drain.
    //     For async worker uploads, copy data into a staging buffer first.
    //
    //   PIPELINE — deferred, cached:
    //     Pipelines are expensive. CreatePipeline is fire-and-forget;
    //     the RenderThread compiles the PSO asynchronously.
    //     IsReady() on the handle reflects compilation status.
    class ENGINE_API RenderInterface
    {
    public:
        static RenderInterface& Get();

        // Buffers

        // Allocate a GPU buffer. If desc.initialData is set, an upload is
        // enqueued automatically for the next frame.
        [[nodiscard]] BufferHandle  CreateBuffer(const BufferDesc& desc);

        // Enqueue a CPU→GPU write. Handle must be valid (not necessarily Ready —
        // the upload will execute after creation if both land in the same frame).
        void UpdateBuffer(BufferHandle handle, const BufferUpdate& update);

        void DestroyBuffer(BufferHandle handle);

        // Meshes

        // Allocates vertex + index buffers and enqueues their uploads.
        // Convenience wrapper over two CreateBuffer + UpdateBuffer calls.
        [[nodiscard]] MeshHandle    CreateMesh(const MeshDesc& desc);

        void DestroyMesh(MeshHandle handle);

        // Textures

        [[nodiscard]] TextureHandle CreateTexture(const ImageDesc& desc);

        // Upload one mip/layer slice. desc.initialData on ImageDesc is NOT
        // supported for textures — use UpdateTexture explicitly.
        void UpdateTexture(TextureHandle handle, const TextureUpdate& update);

        void DestroyTexture(TextureHandle handle);

        // Pipelines

        // PSO compilation is async on RenderThread. Poll IsReady() before use.
        [[nodiscard]] PipelineHandle CreatePipeline(const PipelineDesc& desc);

        void DestroyPipeline(PipelineHandle handle);

        // Query 

        // Returns true if the resource has been materialised on the GPU.
        // Equivalent to handle.IsReady() — provided for generic call sites.
        [[nodiscard]] bool IsReady(BufferHandle   handle) const noexcept { return handle.IsReady(); }
        [[nodiscard]] bool IsReady(TextureHandle  handle) const noexcept { return handle.IsReady(); }
        [[nodiscard]] bool IsReady(PipelineHandle handle) const noexcept { return handle.IsReady(); }

    private:
        RenderInterface() = default;
        ~RenderInterface() = default;
        RenderInterface(const RenderInterface&) = delete;
        RenderInterface& operator=(const RenderInterface&) = delete;
    };
}
