#include "pch.h"
#include "VulkanContext.h"
#include "Graphics/backend/Vulkan/Buffer/VulkanBuffer.h"
#include "Engine/Graphics/MemoryDomain.h"
#include "Graphics/backend/Vulkan/ShaderModule.h"
#include "Graphics/backend/Vulkan/Debug/VulkanDebug.h"
#include "Graphics/backend/Vulkan/ShaderProgram.h"
#include "Graphics/backend/Vulkan/Translator/VulkanTranslator.h"
#include "Engine/Graphics/ImGuiFrame.h"
#include "Graphics/core/FrameData.h"
#include <Engine/Core.h>
#include <Engine/Log.h>
#include <Engine/Window.h>
#include <Engine/Graphics/RenderResources.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>

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
        , m_swapchain(&m_device, &m_physicaldevice, &m_surface, window, window->GetPresentModePolicy())
        , m_pipeline(&m_device, &m_swapchain)
        , m_commandcontext(&m_device)
        , m_syncobjects(&m_device, &m_swapchain)
        , m_Window(window)
        , m_currentPolicy(window->GetPresentModePolicy())
    {
        ZERO_CORE_INFO("Initializing Vulkan Device...");
        init();
    }

    VulkanDevice::~VulkanDevice()
    {
        //cleanupSwapChain();
    }

    void VulkanDevice::init() {
        m_memoryAllocator.Initialize(*m_instance.Get(), *m_physicaldevice.Get(), *m_device.Get());

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

        ZERO_CORE_INFO("Memory allocator succesfully initialized");

        ZERO_CORE_INFO("Vulkan Device initialized successfully.");
    }

    void VulkanDevice::SwapBuffers()
    {

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
    bool VulkanDevice::BeginFrame()
    {
        PresentModePolicy requestedPolicy = m_Window->GetPresentModePolicy();
        if (m_currentPolicy != requestedPolicy)
        {
            m_currentPolicy = requestedPolicy;
            m_SwapChainDirty = true;
        }

        if (m_SwapChainDirty)
        {
            m_SwapChainDirty = false;
            m_device.Get().waitIdle(); // Stall GPU
            recreateSwapChain();
            return false;
        }

        // Wait for the previus frame to complete
        auto fenceResult = m_device.Get().waitForFences(*m_syncobjects.GetInFlightFences()[currentFrame], vk::True, UINT64_MAX);

        ZERO_CORE_ASSERT(fenceResult == vk::Result::eSuccess, "failed to wait for fence");

        // Grab an image from the framebuffer
        // First parameter indices the timeout in nanoseconds, beign the max unsigned 64 bit integer effictivily disable the timeout
        // The next to parameters are the syncronizations objects to be signaled when the presentation engine is finished using the image
        // It is possible to specify a semaphore, a fence or both.
        // The last paramater specifies a variable to output the index of the swap chain image that has become avaible
        auto [result, imageIndex] = m_swapchain.Get().acquireNextImage(UINT64_MAX, *m_syncobjects.GetImageAvailableSemaphores()[currentFrame], nullptr);

        if (result == vk::Result::eErrorOutOfDateKHR)
        {
            m_SwapChainDirty = true;
            return false;
        }

        if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR)
        {
            ZERO_CORE_WARN("Failed to acquire swap chain image");
            currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT; // avanza
            return false;
        }

        m_currentImageIndex = imageIndex;

        return true;
    }

    void VulkanDevice::InitImGui()
    {
        vk::DescriptorPoolSize poolSizes[] = {
            {vk::DescriptorType::eSampler, 1000},
            {vk::DescriptorType::eCombinedImageSampler, 1000},
            {vk::DescriptorType::eSampledImage, 1000},
            {vk::DescriptorType::eStorageImage, 1000},
            {vk::DescriptorType::eUniformTexelBuffer, 1000},
            {vk::DescriptorType::eStorageTexelBuffer, 1000},
            {vk::DescriptorType::eUniformBuffer, 1000},
            {vk::DescriptorType::eStorageBuffer, 1000},
            {vk::DescriptorType::eUniformBufferDynamic, 1000},
            {vk::DescriptorType::eStorageBufferDynamic, 1000},
            {vk::DescriptorType::eInputAttachment, 1000}
        };

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
        poolInfo.maxSets = 1000 * 11;
        poolInfo.poolSizeCount = static_cast<uint32_t>(std::size(poolSizes));
        poolInfo.pPoolSizes = poolSizes;

        m_ImGuiDescriptorPool = vk::raii::DescriptorPool(m_device.Get(), poolInfo);

        ImGui_ImplVulkan_InitInfo initInfo = {};
        initInfo.Instance = *m_instance.Get();
        initInfo.PhysicalDevice = *m_physicaldevice.Get();
        initInfo.Device = *m_device.Get();
        initInfo.QueueFamily = m_device.GetFamilyGraphicsIndex();
        initInfo.Queue = *m_device.GetGraphicsQueue();
        initInfo.PipelineCache = nullptr;
        initInfo.DescriptorPool = *m_ImGuiDescriptorPool;
        initInfo.MinImageCount = MAX_FRAMES_IN_FLIGHT;
        initInfo.ImageCount = MAX_FRAMES_IN_FLIGHT;
        initInfo.Allocator = nullptr;
        initInfo.CheckVkResultFn = nullptr;
        initInfo.UseDynamicRendering = true;
        
        initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        static VkFormat colorFormat;
        colorFormat = static_cast<VkFormat>(m_swapchain.GetFormat());
        initInfo.PipelineInfoMain.PipelineRenderingCreateInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR };
        initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
        initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &colorFormat;

        ImGui_ImplVulkan_Init(&initInfo);
    }

    void VulkanDevice::ShutdownImGui()
    {
        ImGui_ImplVulkan_Shutdown();
        m_ImGuiDescriptorPool.clear();
    }

    void VulkanDevice::RenderFrame(FrameData* frame)
    {
        m_device.Get().resetFences(*m_syncobjects.GetInFlightFences()[currentFrame]);
        m_commandcontext.GetCommandBuffers()[currentFrame].reset();
        recordCommandBuffer(m_currentImageIndex, frame);

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
        m_swapchain.recreateSwapChain(m_currentPolicy);
        m_syncobjects.recreateSyncObjects();
    }

    void VulkanDevice::recordCommandBuffer(uint32_t imageIndex, FrameData* frame)
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

        attachmentInfo.imageView = *m_swapchain.GetImageViews()[imageIndex];
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

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_pipeline.GetGraphicsPipeline());

        // Set dynamic data
        commandBuffer.setViewport(0, vk::Viewport(0.0f, 0.0f, static_cast<float>(m_swapchain.GetExtent().width), static_cast<float>(m_swapchain.GetExtent().height), 0.0f, 1.0f));
        commandBuffer.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), m_swapchain.GetExtent()));

        commandBuffer.draw(3, 1, 0, 0);

        ImDrawData* drawData = frame->imguiFrame.GetNativeDrawData();
        if (drawData)
        {
            ImGui_ImplVulkan_RenderDrawData(drawData, *commandBuffer);
        }

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

    Pipeline* VulkanDevice::CreatePipeline(const PipelineDesc& desc)
    {
        ZERO_CORE_ERROR("Pipeline creation not implemented");
        return nullptr;
    }

    Buffer* VulkanDevice::CreateBuffer(const BufferDesc& desc)
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = desc.size;
        bufferInfo.usage = Vulkan::toVkBufferUsage(desc.usage);
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = Vulkan::toVmaMemoryUsage(desc.memory);

        // Persistently map host-visible memory
        if (desc.memory != MemoryDomain::GPU)
        {
            allocInfo.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
        }

        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;

        VkResult result = vmaCreateBuffer(
            m_memoryAllocator.GetAllocator(),
            &bufferInfo,
            &allocInfo,
            &buffer,
            &allocation,
            nullptr
        );

        if (result != VK_SUCCESS)
        {
            ZERO_CORE_ERROR("Failed to create Vulkan Buffer");
            return nullptr;
        }

        VulkanBuffer* vulkanBuffer = new VulkanBuffer(&m_memoryAllocator, buffer, allocation, desc.size, desc.usage, desc.memory);

        if (desc.initialData)
        {
            if (vulkanBuffer->IsMappable())
            {
                // VMA already mapped it due to VMA_ALLOCATION_CREATE_MAPPED_BIT
                VmaAllocationInfo allocResult;
                vmaGetAllocationInfo(m_memoryAllocator.GetAllocator(), allocation, &allocResult);
            
                if (allocResult.pMappedData)
                {
                    memcpy(allocResult.pMappedData, desc.initialData, desc.initialDataSize);
                    
                    if (desc.memory == MemoryDomain::CPUtoGPU)
                    {
                        vmaFlushAllocation(m_memoryAllocator.GetAllocator(), allocation, 0, desc.initialDataSize);
                    }
                }
            }
            else
            {
                ZERO_CORE_WARN("initialData provided for GPU-only buffer without Staging Buffer support yet.");
            }
        }

#ifdef ZERO_DEBUG
        if (desc.debugName)
        {
            vmaSetAllocationName(m_memoryAllocator.GetAllocator(), allocation, desc.debugName);

            // Extract the raw C handle from the RAII wrapper
            VkDevice rawDevice = *m_device.Get();

            if (rawDevice != VK_NULL_HANDLE) 
            {
                VkDebugUtilsObjectNameInfoEXT nameInfo{};
                nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
                nameInfo.objectType = VK_OBJECT_TYPE_BUFFER;
                nameInfo.objectHandle = reinterpret_cast<uint64_t>(buffer);
                nameInfo.pObjectName = desc.debugName;

                auto vkSetDebugUtilsObjectNameEXT = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
                    vkGetDeviceProcAddr(rawDevice, "vkSetDebugUtilsObjectNameEXT"));
                
                if (vkSetDebugUtilsObjectNameEXT)
                {
                    vkSetDebugUtilsObjectNameEXT(rawDevice, &nameInfo);
                }
            }
        }
#endif

        return vulkanBuffer;
    }

    void VulkanDevice::DestroyBuffer(Buffer* buffer)
    {
        if (buffer)
        {
            delete buffer;
        }
    }

    void VulkanDevice::CopyBuffer(Buffer* src, size_t srcOffset, Buffer* dst, size_t dstOffset, size_t sizeBytes)
    {
        if (!src || !dst) return;
        
        VulkanBuffer* vkSrc = static_cast<VulkanBuffer*>(src);
        VulkanBuffer* vkDst = static_cast<VulkanBuffer*>(dst);
        
        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = srcOffset;
        copyRegion.dstOffset = dstOffset;
        copyRegion.size = sizeBytes;

        // m_transferCmdBuffer must be managed by VulkanDevice (vkBeginCommandBuffer called at BeginFrame)
        //vkCmdCopyBuffer(m_transferCmdBuffer, vkSrc->GetHandle(), vkDst->GetHandle(), 1, &copyRegion);
    }

    void VulkanDevice::FlushTransfers()
    {
        // Ensure all transfer writes are visible to subsequent reads in the pipeline
        VkMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | 
                            VK_ACCESS_INDEX_READ_BIT | 
                            VK_ACCESS_UNIFORM_READ_BIT | 
                            VK_ACCESS_SHADER_READ_BIT;
        
        /*
        vkCmdPipelineBarrier(
            m_transferCmdBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0,
            1, &barrier,
            0, nullptr,
            0, nullptr
        );
        */

        // After this VulkanDevice should either submit m_transferCmdBuffer to the queue,
    }
}
