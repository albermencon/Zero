#include "pch.h"
#include <Engine/Window.h>
#include "OpenGLDevice.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>

namespace Zero
{
    OpenGLDevice::OpenGLDevice(Window* window)
        : m_window(window)
    {
        init();
    }

    void OpenGLDevice::init()
    {

    }

    void OpenGLDevice::SwapBuffers()
    {
        GLFWwindow* glfwWindow = static_cast<GLFWwindow*>(m_window->GetNativeWindow());
        glfwSwapBuffers(glfwWindow);
    }

    void OpenGLDevice::BeginFrame()
    {

    }
    void OpenGLDevice::EndFrame()
    {

    }

    void OpenGLDevice::OnFinished()
    {

    }
    void OpenGLDevice::OnRenderSurfaceResize()
    {
        m_SwapChainDirty = true;
    }

    Buffer* OpenGLDevice::CreateBuffer(const BufferDesc& desc)
    {
        return nullptr;
    }

    void OpenGLDevice::DestroyBuffer(Buffer* buffer)
    {
    }

    void OpenGLDevice::UpdateBufferData(Buffer* buffer, const void* data, size_t offsetBytes, size_t sizeBytes)
    {
    }
}
