#pragma once
#include "Graphics/core/GraphicsDevice.h"

namespace VoxelEngine
{
    class Window;

    class OpenGLDevice : public GraphicsDevice
    {
    public:
        OpenGLDevice(Window* window);

        virtual void init() override;         // <--- Importante: añadir override
        virtual void SwapBuffers() override;

        virtual void BeginFrame() override;
        virtual void EndFrame() override;

        virtual void OnFinished() override;
    };
}
