#pragma once
#include <vector>
#include <Engine/Graphics/RHITypes.h>
#include "Graphics/core/GraphicsDevice.h"
#include "Graphics/core/GraphicsCore.h"
#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

#include "Graphics/backend/Vulkan/Device/VulkanSurface.h"
#include "Graphics/backend/Vulkan/Device/VulkanInstance.h"
#include "Graphics/backend/Vulkan/Device/VulkanPhysicalDevice.h"
#include "Graphics/backend/Vulkan/Device/VulkanLogicalDevice.h"
#include "Graphics/backend/Vulkan/Swapchain/VulkanSwapchain.h"
#include "Graphics/backend/Vulkan/Pipeline/VulkanGraphicsPipeline.h"
#include "Graphics/backend/Vulkan/Pipeline/VulkanComputePipeline.h"
#include "Graphics/backend/Vulkan/Command/VulkanCommandContext.h"
#include "Graphics/backend/Vulkan/Sync/VulkanSyncObjects.h"
#include "Graphics/backend/Vulkan/VMA/VulkanMemoryAllocator.h"

namespace Zero
{
    enum class PresentModePolicy;
    class Window;

    class VulkanDevice : public GraphicsDevice
    {
    public:
        VulkanDevice(Window* window);
        ~VulkanDevice();

        virtual void init() override;
        virtual void SwapBuffers() override;

        virtual bool BeginFrame() override;
        virtual void RenderFrame(class FrameData* frame) override;

        virtual void InitImGui() override;
        virtual void ShutdownImGui() override;
        virtual void UpdateImGuiTextures() override;

        virtual void OnFinished() override;

        virtual void OnRenderSurfaceResize() override;

        virtual Buffer* CreateBuffer(const BufferDesc& desc) override;
        virtual void DestroyBuffer(Buffer* buffer) override;
        virtual void UpdateBuffer(Buffer* dst, size_t dstOffset, const void* data, size_t sizeBytes) override;
        virtual void CopyBuffer(Buffer* src, size_t srcOffset, Buffer* dst, size_t dstOffset, size_t sizeBytes) override;

        virtual GraphicsPipeline* CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) override;
        virtual void DestroyGraphicsPipeline(GraphicsPipeline* pipeline) override;

        virtual ComputePipeline* CreateComputePipeline(const ComputePipelineDesc& desc) override;
        virtual void DestroyComputePipeline(ComputePipeline* pipeline) override;

        virtual void FlushTransfers() override;

        void recreateSwapChain();

        void recordCommandBuffer(uint32_t imageIndex, class FrameData* frame);
        void recordPendingTransfers(vk::raii::CommandBuffer& commandBuffer);
        void executeComputeDispatches(vk::raii::CommandBuffer& commandBuffer, const std::vector<RHI::ComputeDispatch>& dispatches);
        void emitComputeBarrier(vk::raii::CommandBuffer& commandBuffer, RHI::ComputeBarrier barrier);
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
        void transition_depth_layout(
            vk::ImageLayout oldLayout,
            vk::ImageLayout newLayout,
            vk::AccessFlags2 srcAccessMask,
            vk::AccessFlags2 dstAccessMask,
            vk::PipelineStageFlags2 srcStageMask,
            vk::PipelineStageFlags2 dstStageMask,
            vk::raii::CommandBuffer& commandBuffer
        );
    private:
        void createDepthResources();
        void destroyDepthResources();

        VkImage m_depthImage = VK_NULL_HANDLE;
        VmaAllocation m_depthAllocation = VK_NULL_HANDLE;
        vk::raii::ImageView m_depthImageView{nullptr};
        vk::Format m_depthFormat = vk::Format::eD32Sfloat;
    private:
        struct PendingStagingCopy
        {
            VkBuffer dstBuffer = VK_NULL_HANDLE;
            VkDeviceSize srcOffset = 0;
            VkDeviceSize dstOffset = 0;
            VkDeviceSize size = 0;
        };

        struct PendingBufferCopy
        {
            VkBuffer srcBuffer = VK_NULL_HANDLE;
            VkBuffer dstBuffer = VK_NULL_HANDLE;
            VkDeviceSize srcOffset = 0;
            VkDeviceSize dstOffset = 0;
            VkDeviceSize size = 0;
        };

        static constexpr size_t STAGING_SLAB_SIZE = 32 * 1024 * 1024;
        static constexpr size_t STAGING_BUFFER_SIZE = STAGING_SLAB_SIZE * MAX_FRAMES_IN_FLIGHT;

        VkBuffer m_stagingBuffer = VK_NULL_HANDLE;
        VmaAllocation m_stagingAllocation = VK_NULL_HANDLE;
        uint8_t* m_stagingMappedPtr = nullptr;
        size_t m_slabCursor = 0;

        std::vector<PendingStagingCopy> m_pendingStagingCopies;
        std::vector<PendingBufferCopy> m_pendingBufferCopies;
    private:
        VulkanInstance m_instance;
        VulkanSurface m_surface;
        VulkanPhysicalDevice m_physicaldevice;
        VulkanLogicalDevice m_device;
        VulkanSwapchain m_swapchain;
        VulkanCommandContext m_commandcontext;
        VulkanSyncObjects m_syncobjects;
        VulkanMemory m_memoryAllocator;
        vk::raii::DescriptorPool m_ImGuiDescriptorPool{nullptr};
    private:
        VkFormat m_cachedSwapchainFormat = VK_FORMAT_UNDEFINED;
        uint32_t m_currentImageIndex = 0;
        PresentModePolicy m_currentPolicy;

    private:
        uint32_t currentFrame = 0;
    private:
        Window* m_Window;
        bool m_SwapChainDirty = false;
    };
}
