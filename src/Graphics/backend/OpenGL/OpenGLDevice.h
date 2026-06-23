#pragma once
#include "Graphics/core/GraphicsDevice.h"

namespace Zero
{
    class Window;

    class OpenGLDevice : public GraphicsDevice
    {
    public:
        OpenGLDevice(Window* window);
        virtual ~OpenGLDevice() override;

        virtual void init() override;
        virtual void SwapBuffers() override;

        virtual void BeginFrame() override;
        virtual void EndFrame() override;

        virtual void OnFinished() override;

        virtual Buffer* CreateBuffer(const BufferDesc& desc) override;
        virtual void DestroyBuffer(Buffer* buffer) override;
        virtual void UpdateBufferData(Buffer* buffer, const void* data, size_t offsetBytes, size_t sizeBytes) override;

        virtual void OnRenderSurfaceResize() override;

    private:
        Window* m_window = nullptr;
        bool m_SwapChainDirty = false;
        unsigned int m_shaderProgram = 0;
        unsigned int m_vao = 0;
        bool m_contextMadeCurrentOnRenderThread = false;
    };
}
