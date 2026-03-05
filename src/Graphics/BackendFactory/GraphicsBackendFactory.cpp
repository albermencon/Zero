#include "pch.h"
#include "Graphics/BackendFactory/GraphicsBackendFactory.h"
#include <memory>
#include <Engine/Window.h>
#include <Engine/Log.h>
#include <Engine/Core.h>

#if defined(PLATFORM_WINDOWS)
#include "Graphics/backend/Vulkan/VulkanContext.h"
#include "Graphics/backend/OpenGL/OpenGLDevice.h"
#elif defined(PLATFORM_MACOS)
#error Unsupported platform
#elif defined(PLATFORM_LINUX)
#include "Graphics/backend/Vulkan/VulkanContext.h"
#include "Graphics/backend/OpenGL/OpenGLDevice.h"
#else
#error Unsupported platform
#endif

namespace Zero
{
    std::unique_ptr<GraphicsDevice> GraphicsBackend::Create(
        BackendType type,
        Window& window)
    {
#if defined(PLATFORM_WINDOWS)
        switch (type)
        {
        case BackendType::Vulkan:
            return std::make_unique<VulkanDevice>(&window);

        case BackendType::OpenGL:
            return std::make_unique<OpenGLDevice>(&window);

        case BackendType::DirectX12:
            return nullptr;
            //return std::make_unique<D3D12Backend>(nativeWindowHandle
        }

#elif defined(PLATFORM_MACOS)

        switch (type)
        {
        case BackendType::Vulkan:
            return std::make_unique<VulkanDevice>(nativeWindowHandle);

        case BackendType::Metal:
            return std::make_unique<MetalBackend>(nativeWindowHandle);

        case BackendType::OpenGL:
            return std::make_unique<OpenGLDevice>(nativeWindowHandle);
        }

#elif defined(PLATFORM_LINUX)

        switch (type)
        {
        case BackendType::Vulkan:
            return std::make_unique<VulkanDevice>(nativeWindowHandle);

        case BackendType::OpenGL:
            return std::make_unique<OpenGLDevice>(nativeWindowHandle);
        }

#else
#error Unsupported platform
#endif

        ENGINE_CORE_ERROR("Invalid backend type.");
        return nullptr;
    }
}
