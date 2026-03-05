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
        virtual void BeginFrame() = 0;
        virtual void EndFrame() = 0;

        virtual void init() = 0;
        virtual void SwapBuffers() = 0;

        virtual void OnFinished() = 0;

        virtual void OnRenderSurfaceResize() = 0;
    };

}
