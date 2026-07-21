#pragma once
#include "Graphics/core/GraphicsDevice.h"
#include "Engine/Window.h"

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

        virtual bool BeginFrame() override;
        virtual void RenderFrame(class FrameData* frame) override;

        virtual void InitImGui() override;
        virtual void ShutdownImGui() override;

        virtual void OnFinished() override;

        virtual Buffer* CreateBuffer(const BufferDesc& desc) override;
        virtual void DestroyBuffer(Buffer* buffer) override;
        virtual void CopyBuffer(Buffer* src, size_t srcOffset, Buffer* dst, size_t dstOffset, size_t sizeBytes) override;

        virtual void FlushTransfers() override;

        virtual void OnRenderSurfaceResize() override;

    private:
        Window* m_window = nullptr;
        bool m_SwapChainDirty = false;
        unsigned int m_shaderProgram = 0;
        unsigned int m_vao = 0;
        bool m_contextMadeCurrentOnRenderThread = false;
        PresentModePolicy m_currentPolicy = PresentModePolicy::VSync;
        bool m_policyInitialized = false;
    };
}
