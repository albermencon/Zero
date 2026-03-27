#include "pch.h"
#include <Engine/Core.h>
#include <Engine/Window.h>
#include <Engine/Log.h>
#include "VulkanContext.h"
#include <map>
#include <GLFW/glfw3.h>

#include "Graphics/backend/Vulkan/ShaderModule.h"
#include "Graphics/backend/Vulkan/ShaderProgram.h"
#include "Graphics/backend/Vulkan/VulkanDebug.h"

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

/*
* TODO: Create a validation system for extensions and layers
* TODO: Make present mode configurable
*/

namespace Zero
{
    VulkanDevice::VulkanDevice(Window* window)
        : m_instance()
        , m_surface(m_instance.Get(), *window)
        , m_physicaldevice(m_instance.Get())
        , m_device(m_surface.Get(), m_physicaldevice.Get())
        , m_swapchain(&m_device, &m_physicaldevice, &m_surface, window)
        , m_pipeline(&m_device, &m_swapchain)
        , m_commandcontext(&m_device)
        , m_syncobjects(&m_device, &m_swapchain)
        , m_Window(window)
    {
        ENGINE_CORE_INFO("Initializing Vulkan Device...");
        init();
    }

    VulkanDevice::~VulkanDevice()
    {
        //cleanupSwapChain();
    }

    void VulkanDevice::init() {
        m_memoryAllocator.Initialize(*m_instance.Get(), *m_physicaldevice.Get(), *m_device.Get());

        auto alloc = m_memoryAllocator.GetAllocator();

        VkDeviceSize size = 1024ull * 1024ull * 1024ull; // 1 GB

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
            VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;

        /*
        VkResult result = vmaCreateBuffer(
            m_memoryAllocator.GetAllocator(),
            &bufferInfo,
            &allocInfo,
            &buffer,
            &allocation,
            nullptr
        );
        */

        // ImGui
        vk::DescriptorPoolSize poolSize{
            vk::DescriptorType::eCombinedImageSampler, 1
        };
        vk::DescriptorPoolCreateInfo poolInfo{
            vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
            1,          // maxSets
            1,          // poolSizeCount
            &poolSize
        };

        ENGINE_CORE_INFO("Memory allocator succesfully initialized");

        ENGINE_CORE_INFO("Vulkan Device initialized successfully.");
    }

    void VulkanDevice::SwapBuffers()
    {
        if (m_frameAborted) return;

        vk::PresentInfoKHR presentInfo{};
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &*m_syncobjects.GetRenderFinishedSemaphores()[m_currentImageIndex];
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &*m_swapchain.Get();
        presentInfo.pImageIndices = &m_currentImageIndex;

        auto result = m_device.GetPresentQueue().presentKHR(presentInfo);

        if (result == vk::Result::eErrorOutOfDateKHR ||
            result == vk::Result::eSuboptimalKHR)
        {
            m_SwapChainDirty = true; // will be recreated in the next BeginFrame call
        }

        currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }
    void VulkanDevice::BeginFrame()
    {
        // Wait for the previus frame to complete
        auto fenceResult = m_device.Get().waitForFences(*m_syncobjects.GetInFlightFences()[currentFrame], vk::True, UINT64_MAX);

        ENGINE_CORE_ASSERT(fenceResult == vk::Result::eSuccess, "failed to wait for fence");

        // Grab an image from the framebuffer
        // First parameter indices the timeout in nanoseconds, beign the max unsigned 64 bit integer effictivily disable the timeout
        // The next to parameters are the syncronizations objects to be signaled when the presentation engine is finished using the image
        // It is possible to specify a semaphore, a fence or both.
        // The last paramater specifies a variable to output the index of the swap chain image that has become avaible
        auto [result, imageIndex] = m_swapchain.Get().acquireNextImage(UINT64_MAX, *m_syncobjects.GetImageAvailableSemaphores()[currentFrame], nullptr);

        if (result == vk::Result::eErrorOutOfDateKHR || m_SwapChainDirty)
        {
            m_SwapChainDirty = false;
            recreateSwapChain();
            m_frameAborted = true;
            return;
        }

        if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR)
        {
            ENGINE_CORE_WARN("Failed to acquire swap chain image");
            currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT; // avanza
            m_frameAborted = true;
            return;
        }

        m_currentImageIndex = imageIndex;
        m_frameAborted = false;

    }
    void VulkanDevice::EndFrame()
    {
        if (m_frameAborted) return;

        m_device.Get().resetFences(*m_syncobjects.GetInFlightFences()[currentFrame]);
        m_commandcontext.GetCommandBuffers()[currentFrame].reset();
        recordCommandBuffer(m_currentImageIndex);

        vk::PipelineStageFlags waitMask(vk::PipelineStageFlagBits::eColorAttachmentOutput);
        vk::SubmitInfo submitInfo{};
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &*m_syncobjects.GetImageAvailableSemaphores()[currentFrame];
        submitInfo.pWaitDstStageMask = &waitMask;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &*m_commandcontext.GetCommandBuffers()[currentFrame];
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &*m_syncobjects.GetRenderFinishedSemaphores()[m_currentImageIndex];

        m_device.GetGraphicsQueue().submit(submitInfo,
            *m_syncobjects.GetInFlightFences()[currentFrame]);
    }

    void VulkanDevice::OnFinished()
    {
        m_device.Get().waitIdle();
    }

    void VulkanDevice::OnRenderSurfaceResize()
    {
        m_SwapChainDirty = true;
    }

    void VulkanDevice::recreateSwapChain()
    {
        m_swapchain.recreateSwapChain();
        m_syncobjects.recreateSyncObjects();
    }

    void VulkanDevice::recordCommandBuffer(uint32_t imageIndex)
    {
        std::vector<vk::raii::CommandBuffer>& commandBuffers = m_commandcontext.GetCommandBuffers();
        vk::raii::CommandBuffer& commandBuffer = commandBuffers[currentFrame];

        vk::CommandBufferBeginInfo beginInfo{};
        commandBuffer.begin(beginInfo);

        // Before starting rendering, transition the swapchain image to COLOR_ATTACHMENT_OPTIMAL
        transition_image_layout(
            imageIndex,
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::eColorAttachmentOptimal,
            {},                                                         // srcAccessMask (no need to wait for previous operations)
            vk::AccessFlagBits2::eColorAttachmentWrite,                 // dstAccessMask
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,         // srcStage
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,          // dstStage
            commandBuffer
        );

        vk::ClearValue clearColor = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
        vk::RenderingAttachmentInfo attachmentInfo = {};

        attachmentInfo.imageView = m_swapchain.GetImageViews()[imageIndex];
        attachmentInfo.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
        attachmentInfo.loadOp = vk::AttachmentLoadOp::eClear;
        attachmentInfo.storeOp = vk::AttachmentStoreOp::eStore;
        attachmentInfo.clearValue = clearColor;

        vk::RenderingInfo renderingInfo = {};
        renderingInfo.renderArea = { .offset = { 0, 0 }, .extent = m_swapchain.GetExtent()};
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachments = &attachmentInfo;

        commandBuffer.beginRendering(renderingInfo);

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, m_pipeline.GetGraphicsPipeline());

        // Set dynamic data
        commandBuffer.setViewport(0, vk::Viewport(0.0f, 0.0f, static_cast<float>(m_swapchain.GetExtent().width), static_cast<float>(m_swapchain.GetExtent().height), 0.0f, 1.0f));
        commandBuffer.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), m_swapchain.GetExtent()));

        commandBuffer.draw(3, 1, 0, 0);

        commandBuffer.endRendering();

        // After rendering, transition the swapchain image to PRESENT_SRC
        transition_image_layout(
            imageIndex,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::ePresentSrcKHR,
            vk::AccessFlagBits2::eColorAttachmentWrite,             // srcAccessMask
            {},                                                     // dstAccessMask
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,     // srcStage
            vk::PipelineStageFlagBits2::eBottomOfPipe,               // dstStage
            commandBuffer
        );

        commandBuffer.end();
    }

    void VulkanDevice::transition_image_layout(uint32_t imageIndex, vk::ImageLayout oldLayout, vk::ImageLayout newLayout, vk::AccessFlags2 srcAccessMask, vk::AccessFlags2 dstAccessMask, vk::PipelineStageFlags2 srcStageMask, vk::PipelineStageFlags2 dstStageMask, vk::raii::CommandBuffer& commandBuffer)
    {
        vk::ImageMemoryBarrier2 barrier{};
        // Source synchronization scope
        barrier.srcStageMask = srcStageMask;
        barrier.srcAccessMask = srcAccessMask;

        // Destination synchronization scope
        barrier.dstStageMask = dstStageMask;
        barrier.dstAccessMask = dstAccessMask;

        // Layout transition info
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;

        // Queue ownership transfer (ignored here)
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

        // Target image
        barrier.image = m_swapchain.GetImages()[imageIndex];

        // Subresource range
        barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        vk::DependencyInfo dependencyInfo{};
        dependencyInfo.dependencyFlags = {};
        dependencyInfo.imageMemoryBarrierCount = 1;
        dependencyInfo.pImageMemoryBarriers = &barrier;

        commandBuffer.pipelineBarrier2(dependencyInfo);
    }

}
