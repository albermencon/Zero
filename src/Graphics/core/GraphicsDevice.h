#pragma once
#include <memory>

namespace Zero
{
    class Buffer;
    class Texture;
    class Pipeline; 
    class GraphicsPipeline;
    class ComputePipeline;

    class BufferDesc;
    struct GraphicsPipelineDesc;
    struct ComputePipelineDesc;
    class TextureDesc;

    class GraphicsDevice
    {
    public:
        virtual ~GraphicsDevice() = default;

        virtual GraphicsPipeline* CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) = 0;
        virtual void DestroyGraphicsPipeline(GraphicsPipeline* pipeline) = 0;

        virtual ComputePipeline* CreateComputePipeline(const ComputePipelineDesc& desc) = 0;
        virtual void DestroyComputePipeline(ComputePipeline* pipeline) = 0;

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
        virtual void UpdateImGuiTextures() = 0;

        virtual void init() = 0;
        virtual void SwapBuffers() = 0;

        virtual Buffer* CreateBuffer(const BufferDesc& desc) = 0;
        virtual void DestroyBuffer(Buffer* buffer) = 0;
        virtual void UpdateBuffer(Buffer* dst, size_t dstOffset, const void* data, size_t sizeBytes) = 0;

        // Queues a copy operation. Does NOT block.
        // If dst is CPU-visible, memcpy directly.
        // If dst is GPU-only, memcpy to internal staging buffer and record copy command.
        virtual void CopyBuffer(Buffer* src, size_t srcOffset, Buffer* dst, size_t dstOffset, size_t sizeBytes) = 0;

        virtual void OnFinished() = 0;

        virtual void OnRenderSurfaceResize() = 0;
    };

}
