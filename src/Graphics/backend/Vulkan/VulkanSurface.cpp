#include <pch.h>
#include <Engine/Log.h>
#include "Graphics/backend/Vulkan/VulkanSurface.h"
#include <GLFW/glfw3.h>
#include "VulkanSurface.h"
#include <Engine/Window.h>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

namespace Zero 
{
    VulkanSurface::VulkanSurface(const vk::raii::Instance& instance, const Window& window)
    {
        createSurface(instance, window);
    }

    void VulkanSurface::createSurface(const vk::raii::Instance& instance, const Window& m_window)
    {
        VkSurfaceKHR _surface;

        GLFWwindow* window = static_cast<GLFWwindow*>(m_window.GetNativeWindow());
        if (glfwCreateWindowSurface(*instance, window, nullptr, &_surface) != 0)
        {
            ENGINE_CORE_ERROR("failed to create window surface!");
            throw std::runtime_error("failed to create window surface!");
        }

        m_surface = vk::raii::SurfaceKHR(instance, _surface);
    }
}
