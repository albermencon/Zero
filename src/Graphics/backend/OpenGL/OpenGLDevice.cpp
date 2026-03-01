#include "pch.h"
#include <Engine/Window.h>
#include "OpenGLDevice.h"

namespace VoxelEngine
{
    OpenGLDevice::OpenGLDevice(Window* window)
    {
        init();
    }

    void OpenGLDevice::init() {
        // Tu lógica de inicialización aquí
    }

    void OpenGLDevice::SwapBuffers() {
        // Tu lógica de volcado de buffers aquí
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
}
