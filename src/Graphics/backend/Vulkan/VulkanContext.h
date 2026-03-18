#pragma once
#include "Graphics/core/GraphicsDevice.h"
#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif
#include <optional>

#include "Graphics/backend/Vulkan/VulkanSurface.h"
#include "Graphics/backend/Vulkan/VulkanInstance.h"
#include "Graphics/backend/Vulkan/VulkanPhysicalDevice.h"
#include "Graphics/backend/Vulkan/VulkanLogicalDevice.h"
#include "Graphics/backend/Vulkan/VulkanSwapChain.h"
#include "Graphics/backend/Vulkan/VulkanPipeline.h"
#include "Graphics/backend/Vulkan/VulkanCommandContext.h"
#include "Graphics/backend/Vulkan/VulkanSyncObjects.h"

#include "Graphics/backend/Vulkan/VulkanMemoryAllocator.h"

namespace Zero
{

    class Window;

    class VulkanDevice : public GraphicsDevice
    {
    public:
        VulkanDevice(Window* window);
        ~VulkanDevice();

        virtual void init() override;
        virtual void SwapBuffers() override;

        virtual void BeginFrame() override;
        virtual void EndFrame() override;

        virtual void OnFinished() override;

        virtual void OnRenderSurfaceResize() override;

        void recreateSwapChain();

        void recordCommandBuffer(uint32_t imageIndex);
        void transition_image_layout(
            uint32_t imageIndex,
            vk::ImageLayout oldLayout,
            vk::ImageLayout newLayout,
            vk::AccessFlags2 srcAccessMask,
            vk::AccessFlags2 dstAccessMask,
            vk::PipelineStageFlags2 srcStageMask,
            vk::PipelineStageFlags2 dstStageMask,
            vk::raii::CommandBuffer& commandBuffer
        );
    private:
        void initImGui();
        void shutdownImGui();

        // harcodewd imgui state
        vk::raii::DescriptorPool m_imguiDescPool = nullptr;
        VkFormat m_cachedSwapchainFormat = VK_FORMAT_UNDEFINED;
        bool m_imguiInitialized = false;
        uint32_t m_currentImageIndex = 0;
        bool m_frameAborted = false;

    private:
        VulkanInstance m_instance;
        VulkanSurface m_surface;
        VulkanPhysicalDevice m_physicaldevice;
        VulkanLogicalDevice m_device;
        VulkanSwapchain m_swapchain;
        VulkanPipeline m_pipeline;
        VulkanCommandContext m_commandcontext;
        VulkanSyncObjects m_syncobjects;
        VulkanMemory m_memoryAllocator;
    private:
        uint32_t currentFrame = 0;
        const uint32_t MAX_FRAMES_IN_FLIGHT = 2;
    private:
        Window* m_Window;
        bool m_SwapChainDirty = false;
    };
}
