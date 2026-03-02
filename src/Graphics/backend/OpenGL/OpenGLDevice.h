#pragma once
#include "Graphics/core/GraphicsDevice.h"

namespace VoxelEngine
{
    class Window;

    class OpenGLDevice : public GraphicsDevice
    {
    public:
        OpenGLDevice(Window* window);

        virtual void init() override;
        virtual void SwapBuffers() override;

        virtual void BeginFrame() override;
        virtual void EndFrame() override;

        virtual void OnFinished() override;

        virtual void OnRenderSurfaceResize() override;

    private:
        Window* m_window = nullptr;
        bool m_SwapChainDirty = false;
    };
}
