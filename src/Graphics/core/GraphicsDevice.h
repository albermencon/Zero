#pragma once
#include <memory>

namespace Zero
{
    class Buffer;
    class BufferDesc;

    class GraphicsDevice
    {
    public:
        virtual ~GraphicsDevice() = default;

        //virtual std::shared_ptr<Buffer> CreateBuffer(const BufferDesc& desc) = 0;

        // Frame lifecycle
        virtual bool BeginFrame() = 0;
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
