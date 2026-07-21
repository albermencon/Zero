#pragma once
#include <memory>

namespace Zero
{
    class Buffer;
    class Texture;
    class Pipeline; 

    class BufferDesc;
    class PipelineDesc;
    class TextureDesc;

    class GraphicsDevice
    {
    public:
        virtual ~GraphicsDevice() = default;

        virtual Pipeline* CreatePipeline(const PipelineDesc& desc) = 0;
        //virtual void DestroyPipeline(Pipeline* pipeline) = 0; // ?

        //virtual Texture* CreateTexture(const TextureDesc& desc) = 0;
        //virtual void DeleteTexture(Texture* texture) = 0; // ?

        // Frame lifecycle
        virtual bool BeginFrame() = 0;

        // Executes all queued buffer updates via staging and issues memory barriers.
        // Must be called before RenderFrame.
        virtual void FlushTransfers() = 0;

        // Flushes CPU caches for non-coherent memory domains (CPUtoGPU)
        //virtual void FlushMappedMemory(Buffer* buffer, size_t offsetBytes, size_t sizeBytes) = 0;

        virtual void RenderFrame(class FrameData* frame) = 0;

        virtual void InitImGui() = 0;
        virtual void ShutdownImGui() = 0;

        virtual void init() = 0;
        virtual void SwapBuffers() = 0;

        virtual Buffer* CreateBuffer(const BufferDesc& desc) = 0;
        virtual void DestroyBuffer(Buffer* buffer) = 0;

        // Queues a copy operation. Does NOT block.
        // If dst is CPU-visible, memcpy directly.
        // If dst is GPU-only, memcpy to internal staging buffer and record copy command.
        virtual void CopyBuffer(Buffer* src, size_t srcOffset, Buffer* dst, size_t dstOffset, size_t sizeBytes) = 0;

        virtual void OnFinished() = 0;

        virtual void OnRenderSurfaceResize() = 0;
    };

}
