#include "pch.h"
#include "Graphics/backend/Vulkan/VulkanDevice.h"
#include "Graphics/backend/Vulkan/Buffer/VulkanBuffer.h"
#include "Engine/Graphics/MemoryDomain.h"
#include "Graphics/backend/Vulkan/Translator/VulkanTranslator.h"
#include "Engine/Graphics/ImGuiFrame.h"
#include <Engine/Graphics/FrameData.h>
#include <Engine/Math/Frustum.h>
#include "Graphics/Renderer/RenderInterfaceImpl.h"
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
        m_device.Get().waitIdle();
        m_commandcontext.GetCommandPool().reset();
        destroyDepthResources();
        if (m_stagingBuffer != VK_NULL_HANDLE && m_memoryAllocator.GetAllocator() != VK_NULL_HANDLE)
        {
            vmaDestroyBuffer(m_memoryAllocator.GetAllocator(), m_stagingBuffer, m_stagingAllocation);
            m_stagingBuffer = VK_NULL_HANDLE;
            m_stagingAllocation = VK_NULL_HANDLE;
            m_stagingMappedPtr = nullptr;
        }
    }

    void VulkanDevice::init() {
        m_memoryAllocator.Initialize(*m_instance.Get(), *m_physicaldevice.Get(), *m_device.Get());
        createDepthResources();

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

        VkBufferCreateInfo stagingBufferInfo{};
        stagingBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        stagingBufferInfo.size = STAGING_BUFFER_SIZE;
        stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        stagingBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo stagingAllocInfo{};
        stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo allocResult{};
        VkResult stagingResult = vmaCreateBuffer(
            m_memoryAllocator.GetAllocator(),
            &stagingBufferInfo,
            &stagingAllocInfo,
            &m_stagingBuffer,
            &m_stagingAllocation,
            &allocResult
        );

        if (stagingResult == VK_SUCCESS && allocResult.pMappedData)
        {
            m_stagingMappedPtr = static_cast<uint8_t*>(allocResult.pMappedData);
            ZERO_CORE_INFO("Persistent staging buffer initialized (96 MB across 3 frame slabs).");
        }
        else
        {
            ZERO_CORE_ERROR("Failed to allocate persistent staging buffer.");
        }

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

        m_slabCursor = 0;

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

    void VulkanDevice::UpdateImGuiTextures()
    {
        for (ImTextureData* tex : ImGui::GetPlatformIO().Textures)
        {
            if (tex->Status != ImTextureStatus_OK)
                ImGui_ImplVulkan_UpdateTexture(tex);
        }
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
        destroyDepthResources();
        createDepthResources();
    }

    void VulkanDevice::recordCommandBuffer(uint32_t imageIndex, FrameData* frame)
    {
        std::vector<vk::raii::CommandBuffer>& commandBuffers = m_commandcontext.GetCommandBuffers();
        vk::raii::CommandBuffer& commandBuffer = commandBuffers[currentFrame];

        vk::CommandBufferBeginInfo beginInfo{};
        commandBuffer.begin(beginInfo);

        recordPendingTransfers(commandBuffer);

        if (frame && frame->gpuDriven.enabled && frame->gpuDriven.countBuffer.IsValid())
        {
            auto& impl = RenderInterfaceImpl::Get();
            Buffer* b = impl.bufferRegistry.GetBackendResource(frame->gpuDriven.countBuffer);
            if (b)
            {
                vk::Buffer countBuffer(static_cast<VulkanBuffer*>(b)->GetHandle());
                commandBuffer.fillBuffer(countBuffer, 0, sizeof(uint32_t), 0);

                vk::MemoryBarrier2 fillBarrier{};
                fillBarrier.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
                fillBarrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
                fillBarrier.dstStageMask = vk::PipelineStageFlagBits2::eComputeShader;
                fillBarrier.dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite;

                vk::DependencyInfo depInfo{};
                depInfo.memoryBarrierCount = 1;
                depInfo.pMemoryBarriers = &fillBarrier;
                commandBuffer.pipelineBarrier2(depInfo);
            }
        }

        if (frame && !frame->preComputeDispatches.empty())
        {
            executeComputeDispatches(commandBuffer, frame->preComputeDispatches);
        }

        // Transition depth buffer to DEPTH_ATTACHMENT_OPTIMAL
        transition_depth_layout(
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::eDepthAttachmentOptimal,
            {},
            vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
            vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
            vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
            commandBuffer
        );

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

        vk::ClearValue clearColor = vk::ClearColorValue(0.45f, 0.68f, 0.95f, 1.0f);
        vk::RenderingAttachmentInfo attachmentInfo = {};

        attachmentInfo.imageView = *m_swapchain.GetImageViews()[imageIndex];
        attachmentInfo.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
        attachmentInfo.loadOp = vk::AttachmentLoadOp::eClear;
        attachmentInfo.storeOp = vk::AttachmentStoreOp::eStore;
        attachmentInfo.clearValue = clearColor;

        vk::ClearValue clearDepth = vk::ClearDepthStencilValue(1.0f, 0);
        vk::RenderingAttachmentInfo depthAttachmentInfo{};
        depthAttachmentInfo.imageView = *m_depthImageView;
        depthAttachmentInfo.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal;
        depthAttachmentInfo.loadOp = vk::AttachmentLoadOp::eClear;
        depthAttachmentInfo.storeOp = vk::AttachmentStoreOp::eDontCare;
        depthAttachmentInfo.clearValue = clearDepth;

        vk::RenderingInfo renderingInfo = {};
        renderingInfo.renderArea = { .offset = { 0, 0 }, .extent = m_swapchain.GetExtent()};
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachments = &attachmentInfo;
        renderingInfo.pDepthAttachment = &depthAttachmentInfo;

        commandBuffer.beginRendering(renderingInfo);

        // Set dynamic data
        commandBuffer.setViewport(0, vk::Viewport(0.0f, 0.0f, static_cast<float>(m_swapchain.GetExtent().width), static_cast<float>(m_swapchain.GetExtent().height), 0.0f, 1.0f));
        commandBuffer.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), m_swapchain.GetExtent()));

        if (frame && !frame->drawPackets.empty())
        {
            auto& impl = RenderInterfaceImpl::Get();

            auto getVkBuffer = [&impl](BufferHandle handle) -> vk::Buffer {
                if (!handle.IsValid()) return nullptr;
                Buffer* b = impl.bufferRegistry.GetBackendResource(handle);
                if (!b) return nullptr;
                return vk::Buffer(static_cast<VulkanBuffer*>(b)->GetHandle());
            };

            auto getGraphicsPipeline = [&impl](uint16_t pipelineId) -> VulkanGraphicsPipeline* {
                if (pipelineId == 0) return nullptr;
                Pipeline* p = impl.pipelineRegistry.GetBackendResourceByIndex(pipelineId);
                if (!p || p->IsCompute()) return nullptr;
                return static_cast<VulkanGraphicsPipeline*>(p);
            };

            uint16_t currentPipelineId = 0;
            VulkanGraphicsPipeline* currentPipeline = nullptr;
            BufferHandle currentVb{};
            BufferHandle currentIb{};

            for (const auto& packet : frame->drawPackets)
            {
                uint16_t pipelineId = RHI::GetPipelineIdFromSortKey(packet.sortKey);
                if (pipelineId != currentPipelineId)
                {
                    currentPipelineId = pipelineId;
                    currentPipeline = getGraphicsPipeline(pipelineId);
                    if (currentPipeline && *currentPipeline->GetPipeline())
                    {
                        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *currentPipeline->GetPipeline());
                    }
                }

                if (!currentPipeline || !*currentPipeline->GetPipeline())
                    continue;

                if (packet.geometryId < frame->geometryTable.size())
                {
                    const auto& geom = frame->geometryTable[packet.geometryId];

                    if (geom.vertexBuffer.IsValid())
                    {
                        vk::Buffer vb = getVkBuffer(geom.vertexBuffer);
                        if (!vb)
                            continue;

                        if (geom.vertexBuffer != currentVb)
                        {
                            commandBuffer.bindVertexBuffers(0, vb, { 0 });
                            currentVb = geom.vertexBuffer;
                        }
                    }

                    if (geom.indexBuffer.IsValid())
                    {
                        vk::Buffer ib = getVkBuffer(geom.indexBuffer);
                        if (!ib)
                            continue;

                        if (geom.indexBuffer != currentIb)
                        {
                            commandBuffer.bindIndexBuffer(ib, 0, vk::IndexType::eUint32);
                            currentIb = geom.indexBuffer;
                        }
                    }
                }

                if (currentPipeline->GetPushConstantSize() > 0)
                {
                    struct PushConstants
                    {
                        Mat4 viewProj;
                        Mat4 model;
                    } push;
                    push.viewProj = frame->viewProj;
                    if (packet.transformIndex < frame->transform.size())
                    {
                        push.model = frame->transform[packet.transformIndex];
                    }
                    else
                    {
                        push.model = Mat4::Identity();
                    }

                    uint32_t pushSize = std::min(currentPipeline->GetPushConstantSize(), static_cast<uint32_t>(sizeof(push)));
                    commandBuffer.pushConstants(
                        *currentPipeline->GetPipelineLayout(),
                        currentPipeline->GetPushConstantStages(),
                        0,
                        pushSize,
                        &push
                    );
                }

                switch (static_cast<RHI::DrawCmdType>(packet.cmdType))
                {
                case RHI::DrawCmdType::Arrays:
                    commandBuffer.draw(packet.arrays.vertexCount, 1, packet.arrays.firstVertex, 0);
                    break;

                case RHI::DrawCmdType::Indexed:
                    commandBuffer.drawIndexed(packet.indexed.indexCount, 1, packet.indexed.firstIndex, packet.indexed.vertexOffset, 0);
                    break;

                case RHI::DrawCmdType::Instanced:
                    if (packet.geometryId < frame->geometryTable.size() && frame->geometryTable[packet.geometryId].indexBuffer.IsValid())
                    {
                        commandBuffer.drawIndexed(packet.instanced.indexCount, packet.instanced.instanceCount, packet.instanced.firstIndex, packet.instanced.vertexOffset, packet.instanced.firstInstance);
                    }
                    else
                    {
                        commandBuffer.draw(packet.instanced.indexCount, packet.instanced.instanceCount, packet.instanced.firstIndex, packet.instanced.firstInstance);
                    }
                    break;

                case RHI::DrawCmdType::MultiDrawIndirect:
                {
                    vk::Buffer indirectBuffer = getVkBuffer(BufferHandle{ packet.mdi.indirectBufferId });
                    if (indirectBuffer)
                    {
                        if (packet.geometryId < frame->geometryTable.size() && frame->geometryTable[packet.geometryId].indexBuffer.IsValid())
                        {
                            constexpr uint32_t stride = sizeof(VkDrawIndexedIndirectCommand);
                            commandBuffer.drawIndexedIndirect(indirectBuffer, packet.mdi.offset, packet.mdi.drawCount, stride);
                        }
                        else
                        {
                            constexpr uint32_t stride = sizeof(VkDrawIndirectCommand);
                            commandBuffer.drawIndirect(indirectBuffer, packet.mdi.offset, packet.mdi.drawCount, stride);
                        }
                    }
                    break;
                }

                case RHI::DrawCmdType::MultiDrawIndirectCount:
                {
                    vk::Buffer indirectBuffer = getVkBuffer(BufferHandle{ packet.mdiCount.indirectBufferId });
                    vk::Buffer countBuffer = getVkBuffer(BufferHandle{ packet.mdiCount.countBufferId });
                    if (indirectBuffer && countBuffer)
                    {
                        if (packet.geometryId < frame->geometryTable.size() && frame->geometryTable[packet.geometryId].indexBuffer.IsValid())
                        {
                            constexpr uint32_t stride = sizeof(VkDrawIndexedIndirectCommand);
                            commandBuffer.drawIndexedIndirectCount(
                                indirectBuffer,
                                packet.mdiCount.indirectOffset,
                                countBuffer,
                                packet.mdiCount.countOffset,
                                packet.mdiCount.maxDrawCount,
                                stride
                            );
                        }
                        else
                        {
                            constexpr uint32_t stride = sizeof(VkDrawIndirectCommand);
                            commandBuffer.drawIndirectCount(
                                indirectBuffer,
                                packet.mdiCount.indirectOffset,
                                countBuffer,
                                packet.mdiCount.countOffset,
                                packet.mdiCount.maxDrawCount,
                                stride
                            );
                        }
                    }
                    break;
                }
                }
            }
        }

        ImDrawData* drawData = frame ? frame->imguiFrame.GetNativeDrawData() : nullptr;
        if (drawData)
        {
            ImGui_ImplVulkan_RenderDrawData(drawData, *commandBuffer);
        }

        commandBuffer.endRendering();

        if (frame && !frame->postComputeDispatches.empty())
        {
            executeComputeDispatches(commandBuffer, frame->postComputeDispatches);
        }

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

    GraphicsPipeline* VulkanDevice::CreateGraphicsPipeline(const GraphicsPipelineDesc& desc)
    {
        try
        {
            return new VulkanGraphicsPipeline(&m_device, desc);
        }
        catch (const std::exception& e)
        {
            ZERO_CORE_ERROR("Failed to create Vulkan graphics pipeline: {0}", e.what());
            return nullptr;
        }
    }

    void VulkanDevice::DestroyGraphicsPipeline(GraphicsPipeline* pipeline)
    {
        delete pipeline;
    }

    ComputePipeline* VulkanDevice::CreateComputePipeline(const ComputePipelineDesc& desc)
    {
        try
        {
            return new VulkanComputePipeline(&m_device, desc);
        }
        catch (const std::exception& e)
        {
            ZERO_CORE_ERROR("Failed to create Vulkan compute pipeline: {0}", e.what());
            return nullptr;
        }
    }

    void VulkanDevice::DestroyComputePipeline(ComputePipeline* pipeline)
    {
        delete pipeline;
    }

    Buffer* VulkanDevice::CreateBuffer(const BufferDesc& desc)
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = desc.size;
        bufferInfo.usage = Vulkan::toVkBufferUsage(desc.usage);
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (desc.memory == MemoryDomain::GPU)
        {
            bufferInfo.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        }

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

        if (bufferInfo.usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
        {
            vk::BufferDeviceAddressInfo addrInfo{ buffer };
            vk::DeviceAddress deviceAddress = m_device.Get().getBufferAddress(addrInfo);
            vulkanBuffer->SetDeviceAddress(static_cast<uint64_t>(deviceAddress));
        }

        if (desc.initialData && desc.initialDataSize > 0)
        {
            UpdateBuffer(vulkanBuffer, 0, desc.initialData, desc.initialDataSize);
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

    void VulkanDevice::UpdateBuffer(Buffer* dst, size_t dstOffset, const void* data, size_t sizeBytes)
    {
        if (!dst || !data || sizeBytes == 0) return;

        VulkanBuffer* vkDst = static_cast<VulkanBuffer*>(dst);
        if (vkDst->IsMappable())
        {
            VmaAllocationInfo allocResult{};
            vmaGetAllocationInfo(m_memoryAllocator.GetAllocator(), vkDst->GetAllocation(), &allocResult);

            if (allocResult.pMappedData)
            {
                memcpy(static_cast<uint8_t*>(allocResult.pMappedData) + dstOffset, data, sizeBytes);

                // Direct PCIe write into GPU memory (BAR / ReBAR)    
                if (vkDst->GetMemoryDomain() == MemoryDomain::CPUtoGPU)
                {
                    vmaFlushAllocation(m_memoryAllocator.GetAllocator(), vkDst->GetAllocation(), dstOffset, sizeBytes);
                }
            }
        }
        else
        {
            if (m_stagingMappedPtr)
            {
                const size_t alignedSize = (sizeBytes + 15) & ~size_t(15);
                if (m_slabCursor + alignedSize <= STAGING_SLAB_SIZE)
                {
                    const size_t slabOffset = (currentFrame * STAGING_SLAB_SIZE) + m_slabCursor;
                    memcpy(m_stagingMappedPtr + slabOffset, data, sizeBytes);
                    vmaFlushAllocation(m_memoryAllocator.GetAllocator(), m_stagingAllocation, slabOffset, alignedSize);

                    m_pendingStagingCopies.push_back({
                        vkDst->GetHandle(),
                        static_cast<VkDeviceSize>(slabOffset),
                        static_cast<VkDeviceSize>(dstOffset),
                        static_cast<VkDeviceSize>(sizeBytes)
                    });
                    m_slabCursor += alignedSize;
                }
                else
                {
                    ZERO_CORE_ERROR("Staging slab exhausted: required {0} bytes, available {1} bytes",
                                    alignedSize, STAGING_SLAB_SIZE - m_slabCursor);
                }
            }
            else
            {
                ZERO_CORE_ERROR("Staging buffer uninitialized; dropping update for GPU buffer.");
            }
        }
    }

    void VulkanDevice::CopyBuffer(Buffer* src, size_t srcOffset, Buffer* dst, size_t dstOffset, size_t sizeBytes)
    {
        if (!dst || sizeBytes == 0) return;

        VulkanBuffer* vkDst = static_cast<VulkanBuffer*>(dst);

        if (src)
        {
            VulkanBuffer* vkSrc = static_cast<VulkanBuffer*>(src);
            m_pendingBufferCopies.push_back({
                vkSrc->GetHandle(),
                vkDst->GetHandle(),
                static_cast<VkDeviceSize>(srcOffset),
                static_cast<VkDeviceSize>(dstOffset),
                static_cast<VkDeviceSize>(sizeBytes)
            });
        }
    }

    void VulkanDevice::FlushTransfers()
    {
    }

    void VulkanDevice::recordPendingTransfers(vk::raii::CommandBuffer& commandBuffer)
    {
        if (m_pendingStagingCopies.empty() && m_pendingBufferCopies.empty())
            return;

        for (const auto& copy : m_pendingStagingCopies)
        {
            vk::BufferCopy region{};
            region.srcOffset = copy.srcOffset;
            region.dstOffset = copy.dstOffset;
            region.size = copy.size;
            commandBuffer.copyBuffer(m_stagingBuffer, copy.dstBuffer, region);
        }
        m_pendingStagingCopies.clear();

        for (const auto& copy : m_pendingBufferCopies)
        {
            vk::BufferCopy region{};
            region.srcOffset = copy.srcOffset;
            region.dstOffset = copy.dstOffset;
            region.size = copy.size;
            commandBuffer.copyBuffer(copy.srcBuffer, copy.dstBuffer, region);
        }
        m_pendingBufferCopies.clear();

        vk::MemoryBarrier2 barrier{};
        barrier.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
        barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
        barrier.dstStageMask = vk::PipelineStageFlagBits2::eVertexInput |
                               vk::PipelineStageFlagBits2::eVertexShader |
                               vk::PipelineStageFlagBits2::eFragmentShader |
                               vk::PipelineStageFlagBits2::eComputeShader |
                               vk::PipelineStageFlagBits2::eDrawIndirect;
        barrier.dstAccessMask = vk::AccessFlagBits2::eVertexAttributeRead |
                                vk::AccessFlagBits2::eIndexRead |
                                vk::AccessFlagBits2::eShaderStorageRead |
                                vk::AccessFlagBits2::eShaderStorageWrite |
                                vk::AccessFlagBits2::eUniformRead |
                                vk::AccessFlagBits2::eIndirectCommandRead;

        vk::DependencyInfo depInfo{};
        depInfo.memoryBarrierCount = 1;
        depInfo.pMemoryBarriers = &barrier;

        commandBuffer.pipelineBarrier2(depInfo);
    }

    void VulkanDevice::emitComputeBarrier(vk::raii::CommandBuffer& commandBuffer, RHI::ComputeBarrier barrier)
    {
        vk::MemoryBarrier2 memBarrier{};
        switch (barrier)
        {
        case RHI::ComputeBarrier::ComputeToCompute:
            memBarrier.srcStageMask = vk::PipelineStageFlagBits2::eComputeShader;
            memBarrier.srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite;
            memBarrier.dstStageMask = vk::PipelineStageFlagBits2::eComputeShader;
            memBarrier.dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite;
            break;

        case RHI::ComputeBarrier::ComputeToDrawIndirect:
            memBarrier.srcStageMask = vk::PipelineStageFlagBits2::eComputeShader;
            memBarrier.srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite;
            memBarrier.dstStageMask = vk::PipelineStageFlagBits2::eDrawIndirect | vk::PipelineStageFlagBits2::eVertexShader;
            memBarrier.dstAccessMask = vk::AccessFlagBits2::eIndirectCommandRead | vk::AccessFlagBits2::eShaderStorageRead;
            break;

        case RHI::ComputeBarrier::ComputeToGraphics:
            memBarrier.srcStageMask = vk::PipelineStageFlagBits2::eComputeShader;
            memBarrier.srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite;
            memBarrier.dstStageMask = vk::PipelineStageFlagBits2::eVertexInput |
                                      vk::PipelineStageFlagBits2::eVertexShader |
                                      vk::PipelineStageFlagBits2::eFragmentShader;
            memBarrier.dstAccessMask = vk::AccessFlagBits2::eVertexAttributeRead |
                                      vk::AccessFlagBits2::eIndexRead |
                                      vk::AccessFlagBits2::eShaderStorageRead |
                                      vk::AccessFlagBits2::eUniformRead;
            break;

        default:
            return;
        }

        vk::DependencyInfo depInfo{};
        depInfo.memoryBarrierCount = 1;
        depInfo.pMemoryBarriers = &memBarrier;
        commandBuffer.pipelineBarrier2(depInfo);
    }

    void VulkanDevice::executeComputeDispatches(vk::raii::CommandBuffer& commandBuffer, const std::vector<RHI::ComputeDispatch>& dispatches)
    {
        if (dispatches.empty())
            return;

        auto& impl = RenderInterfaceImpl::Get();
        uint32_t currentPipelineId = ~0u;
        VulkanComputePipeline* currentPipeline = nullptr;

        for (const auto& dispatch : dispatches)
        {
            if (!dispatch.pipeline.IsValid())
                continue;

            if (dispatch.pipeline.GetIndex() != currentPipelineId)
            {
                currentPipelineId = dispatch.pipeline.GetIndex();
                Pipeline* p = impl.pipelineRegistry.GetBackendResource(dispatch.pipeline);
                if (p && p->IsCompute())
                {
                    currentPipeline = static_cast<VulkanComputePipeline*>(p);
                    commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, *currentPipeline->GetPipeline());
                }
                else
                {
                    currentPipeline = nullptr;
                }
            }

            if (!currentPipeline)
                continue;

            if (dispatch.pushConstantSize > 0)
            {
#ifdef ZERO_DEBUG
                if (dispatch.pushConstantSize > sizeof(dispatch.pushConstants) ||
                    (dispatch.pushConstantSize & 3) != 0 ||
                    currentPipeline->GetPushConstantSize() < dispatch.pushConstantSize)
                {
                    ZERO_CORE_ERROR("Malformed compute push constants: size {0}, buffer capacity {1}, pipeline layout {2}",
                                    dispatch.pushConstantSize, sizeof(dispatch.pushConstants), currentPipeline->GetPushConstantSize());
                    continue;
                }
#endif
                if (dispatch.pushConstantSize <= sizeof(dispatch.pushConstants) &&
                    (dispatch.pushConstantSize & 3) == 0 &&
                    currentPipeline->GetPushConstantSize() >= dispatch.pushConstantSize)
                {
                    commandBuffer.pushConstants(
                        *currentPipeline->GetPipelineLayout(),
                        vk::ShaderStageFlagBits::eCompute,
                        0,
                        dispatch.pushConstantSize,
                        dispatch.pushConstants
                    );
                }
            }

            commandBuffer.dispatch(dispatch.groupCountX, dispatch.groupCountY, dispatch.groupCountZ);

            if (dispatch.barrier != RHI::ComputeBarrier::None)
            {
                emitComputeBarrier(commandBuffer, dispatch.barrier);
            }
        }
    }

    void VulkanDevice::transition_depth_layout(vk::ImageLayout oldLayout, vk::ImageLayout newLayout, vk::AccessFlags2 srcAccessMask, vk::AccessFlags2 dstAccessMask, vk::PipelineStageFlags2 srcStageMask, vk::PipelineStageFlags2 dstStageMask, vk::raii::CommandBuffer& commandBuffer)
    {
        if (m_depthImage == VK_NULL_HANDLE) return;

        vk::ImageMemoryBarrier2 barrier{};
        barrier.srcStageMask = srcStageMask;
        barrier.srcAccessMask = srcAccessMask;
        barrier.dstStageMask = dstStageMask;
        barrier.dstAccessMask = dstAccessMask;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = m_depthImage;
        barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        vk::DependencyInfo dependencyInfo{};
        dependencyInfo.imageMemoryBarrierCount = 1;
        dependencyInfo.pImageMemoryBarriers = &barrier;

        commandBuffer.pipelineBarrier2(dependencyInfo);
    }

    void VulkanDevice::createDepthResources()
    {
        m_depthFormat = vk::Format::eD32Sfloat;
        const vk::Extent2D extent = m_swapchain.GetExtent();

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = extent.width;
        imageInfo.extent.height = extent.height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = static_cast<VkFormat>(m_depthFormat);
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        VkResult res = vmaCreateImage(
            m_memoryAllocator.GetAllocator(),
            &imageInfo,
            &allocInfo,
            &m_depthImage,
            &m_depthAllocation,
            nullptr
        );
        ZERO_CORE_ASSERT(res == VK_SUCCESS, "Failed to create depth image!");

        vk::ImageViewCreateInfo viewInfo{};
        viewInfo.image = m_depthImage;
        viewInfo.viewType = vk::ImageViewType::e2D;
        viewInfo.format = m_depthFormat;
        viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        m_depthImageView = vk::raii::ImageView(m_device.Get(), viewInfo);
    }

    void VulkanDevice::destroyDepthResources()
    {
        m_depthImageView.clear();
        if (m_depthImage != VK_NULL_HANDLE && m_memoryAllocator.GetAllocator() != VK_NULL_HANDLE)
        {
            vmaDestroyImage(m_memoryAllocator.GetAllocator(), m_depthImage, m_depthAllocation);
            m_depthImage = VK_NULL_HANDLE;
            m_depthAllocation = VK_NULL_HANDLE;
        }
    }
}
