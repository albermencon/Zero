#include "pch.h"
#include "VulkanSyncObjects.h"
#include "Graphics/backend/Vulkan/VulkanLogicalDevice.h"
#include "Graphics/backend/Vulkan/VulkanSwapchain.h"

namespace VoxelEngine 
{
	VulkanSyncObjects::VulkanSyncObjects(VulkanLogicalDevice* device, VulkanSwapchain* swapchain)
        : m_device(device), m_swapchain(swapchain) 
    {
        createSyncObjetcs();
    }

    void VulkanSyncObjects::recreateSyncObjects()
    {
        m_presentCompletedSemaphores.clear();
        m_renderFinishedSemaphores.clear();
        m_inFlightFences.clear();
        m_imagesInFlight.clear();
        createSyncObjetcs();
    }

    void VulkanSyncObjects::createSyncObjetcs()
	{
        uint32_t imageCount = m_swapchain->GetImages().size();

        // Tracks which fence is using each swapchain image
        m_imagesInFlight.resize(imageCount, VK_NULL_HANDLE);

        // Per-image resources
        for (uint32_t i = 0; i < imageCount; i++)
        {
            m_renderFinishedSemaphores.emplace_back(m_device->Get(), vk::SemaphoreCreateInfo());
        }

        // Per-frame resources
        const auto MAX_FRAMES_IN_FLIGHT = 2;
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            m_presentCompletedSemaphores.emplace_back(m_device->Get(), vk::SemaphoreCreateInfo());

            vk::FenceCreateInfo fenceInfo{};
            fenceInfo.flags = vk::FenceCreateFlagBits::eSignaled;

            m_inFlightFences.emplace_back(m_device->Get(), fenceInfo);
        }
	}

}
