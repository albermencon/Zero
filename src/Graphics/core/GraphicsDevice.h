#pragma once

namespace VoxelEngine
{
    class GraphicsDevice
    {
    public:
        virtual ~GraphicsDevice() = default;

        // Frame lifecycle
        virtual void BeginFrame() = 0;
        virtual void EndFrame() = 0;

        virtual void init() = 0;
        virtual void SwapBuffers() = 0;

        virtual void OnFinished() = 0;

        virtual void OnRenderSurfaceResize() = 0;
    };

}
