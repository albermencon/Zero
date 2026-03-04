#include "pch.h"
#include "Graphics/backend/Vulkan/VulkanSwapchain.h"
#include <Engine/Window.h>
#include <GLFW/glfw3.h> // temp
#include "Graphics/backend/Vulkan/VulkanPhysicalDevice.h"
#include "Graphics/backend/Vulkan/VulkanLogicalDevice.h"
#include "Graphics/backend/Vulkan/VulkanSurface.h"
#include <Engine/Log.h>

namespace VoxelEngine 
{
	VulkanSwapchain::VulkanSwapchain(VulkanLogicalDevice* device, VulkanPhysicalDevice* physicalDevice,
        VulkanSurface* surface, Window* window)
        : m_Device(device), m_PhysicalDevice(physicalDevice), m_Surface(surface), m_window(window)
	{
		createSwapChain();
        createImageViews();
	}

    void VulkanSwapchain::recreateSwapChain()
    {
        int width = 0, height = 0;

        GLFWwindow* window = static_cast<GLFWwindow*>(m_window->GetNativeWindow());
        glfwGetFramebufferSize(window, &width, &height);
        while (width == 0 || height == 0) {
            glfwGetFramebufferSize(window, &width, &height);
            glfwWaitEvents();
        }

        m_Device->Get().waitIdle();

        vk::raii::SwapchainKHR oldSwapchain = std::move(m_Swapchain);
        m_Swapchain = nullptr;
        m_ImageViews.clear();

        createSwapChain(*oldSwapchain); // pass the old handle
        createImageViews();
    }

    void VulkanSwapchain::cleanupSwapChain()
    {
        m_ImageViews.clear();
        m_Swapchain = nullptr;
    }

    void VulkanSwapchain::createSwapChain(vk::SwapchainKHR oldSwapchain)
	{
        auto surfaceCapabilities = m_PhysicalDevice->Get().getSurfaceCapabilitiesKHR(*m_Surface->Get());

        ENGINE_CORE_INFO("Surface supportedUsageFlags: {}",
            vk::to_string(surfaceCapabilities.supportedUsageFlags));

        vk::SurfaceFormatKHR surfaceFormat = chooseSurfaceFormat(m_PhysicalDevice->Get().getSurfaceFormatsKHR(*m_Surface->Get()));

        m_ImageFormat = surfaceFormat.format;
        m_ColorSpace = surfaceFormat.colorSpace;
        m_Extent = chooseSwapExtent(surfaceCapabilities);

        // Request triple buffering when possible
        // Ensures at least 3 images unless the surface requires more
        uint32_t imageCount = std::max(
            3u,
            surfaceCapabilities.minImageCount
        );

        // Clamp to maxImageCount if defined
        // maxImageCount == 0 means no upper limit
        if (surfaceCapabilities.maxImageCount > 0
            && imageCount > surfaceCapabilities.maxImageCount)
        {
            imageCount = surfaceCapabilities.maxImageCount;
        }

        m_PresentMode = chooseSwapChainPresentMode(m_PhysicalDevice->Get().getSurfacePresentModesKHR(*m_Surface->Get()));

        vk::SwapchainCreateInfoKHR swapChainCreateInfo{};
        swapChainCreateInfo
            .setFlags(vk::SwapchainCreateFlagsKHR())
            .setSurface(*m_Surface->Get())
            .setMinImageCount(imageCount)
            .setImageFormat(m_ImageFormat)
            .setImageColorSpace(m_ColorSpace)
            .setImageExtent(m_Extent)
            .setImageArrayLayers(1) // unless developing a stereoscopic 3D application
            .setImageUsage(vk::ImageUsageFlagBits::eColorAttachment)
            .setImageSharingMode(vk::SharingMode::eExclusive)
            .setPreTransform(surfaceCapabilities.currentTransform)
            .setCompositeAlpha(vk::CompositeAlphaFlagBitsKHR::eOpaque)
            .setPresentMode(m_PresentMode)
            .setClipped(vk::True)
            .setOldSwapchain(oldSwapchain)
            ;

        uint32_t graphicsFamilyIndex = m_Device->GetFamilyGraphicsIndex();
        uint32_t presentFamilyIndex = m_Device->GetFamilyPresentIndex();
        uint32_t queueFamilyIndices[] = { graphicsFamilyIndex, presentFamilyIndex };

        if (graphicsFamilyIndex != presentFamilyIndex) {
            swapChainCreateInfo.imageSharingMode = vk::SharingMode::eConcurrent;
            swapChainCreateInfo.queueFamilyIndexCount = 2;
            swapChainCreateInfo.pQueueFamilyIndices = queueFamilyIndices;
        }
        else {
            swapChainCreateInfo.imageSharingMode = vk::SharingMode::eExclusive;
            swapChainCreateInfo.queueFamilyIndexCount = 0; // Optional
            swapChainCreateInfo.pQueueFamilyIndices = nullptr; // Optional
        }

        m_Swapchain = vk::raii::SwapchainKHR(m_Device->Get(), swapChainCreateInfo);
        m_Images = m_Swapchain.getImages();
	}

    void VulkanSwapchain::createImageViews()
    {
        m_ImageViews.clear();
        m_ImageViews.reserve(m_Images.size());

        for (size_t i = 0; i < m_Images.size(); ++i)
        {
            vk::ImageViewCreateInfo createInfo{};

            createInfo
                .setImage(m_Images[i])
                .setViewType(vk::ImageViewType::e2D)
                .setFormat(m_ImageFormat)
                .setComponents(vk::ComponentMapping{
                    vk::ComponentSwizzle::eIdentity,
                    vk::ComponentSwizzle::eIdentity,
                    vk::ComponentSwizzle::eIdentity,
                    vk::ComponentSwizzle::eIdentity
                    })
                .setSubresourceRange(vk::ImageSubresourceRange{
                    vk::ImageAspectFlagBits::eColor,
                    0,  // baseMipLevel
                    1,  // levelCount
                    0,  // baseArrayLayer
                    1   // layerCount
                    });

            m_ImageViews.emplace_back(m_Device->Get(), createInfo);
        }
    }

    vk::SurfaceFormatKHR VulkanSwapchain::chooseSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& avaibleFormats)
    {
        // Search for the preferred 8-bit BGRA format with sRGB encoding
        // This ensures proper gamma-correct rendering and presentation
        for (const auto& availableFormat : avaibleFormats)
        {
            if (availableFormat.format == vk::Format::eB8G8R8A8Srgb &&
                availableFormat.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear)
            {
                return availableFormat;
            }
        }

        ENGINE_CORE_WARN("Defaulted");

        // Vulkan always gurantees at least one value for swapchain creation
        return avaibleFormats[0];
    }

    vk::PresentModeKHR VulkanSwapchain::chooseSwapChainPresentMode(const std::vector<vk::PresentModeKHR>& availablePresentModes)
    {
        // TODO: Make present mode configurable.
        // Desktop: MAILBOX (low latency, no tearing)
        // Mobile: FIFO (better power efficiency)
        // Fallback: FIFO (always supported)
        // Present mode affects latency, power consumption, and frame pacing.
        for (const auto& presentMode : availablePresentModes)
        {
            if (presentMode == vk::PresentModeKHR::eMailbox)
            {
                return presentMode;
            }
        }

        for (const auto& presentMode : availablePresentModes)
        {
            if (presentMode == vk::PresentModeKHR::eFifoLatestReady)
            {
                return presentMode;
            }
        }

        for (const auto& presentMode : availablePresentModes)
        {
            if (presentMode == vk::PresentModeKHR::eImmediate)
            {
                return presentMode;
            }
        }

        return vk::PresentModeKHR::eFifo; // fifo is the only guaranteed
    }

    vk::Extent2D VulkanSwapchain::chooseSwapExtent(const vk::SurfaceCapabilitiesKHR& capabilities)
    {
        // The size is fixed by the window system (Wayland, Android, or exclusive fullscreen)
        // Vulkan requires using this exact extent, and the application cannot override it.
        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
        {
            return capabilities.currentExtent;
        }

        // Otherwise, the surface allows the application to choose the swapchain extent
        // Query the current framebuffer size from the window system
        auto [width, height] = m_window->GetFrameBufferSize();

        // Vulkan enforces limits defined in minImageExtent and MaxImageExtent
        // Clamp to valid values preventing swapchain creation failure or validation erros
        return {
            std::clamp<uint32_t>(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
            std::clamp<uint32_t>(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height)
        };
    }
}
