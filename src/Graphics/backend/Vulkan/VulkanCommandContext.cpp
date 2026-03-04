#include "pch.h"
#include "Graphics/backend/Vulkan/VulkanCommandContext.h"
#include "Graphics/backend/Vulkan/VulkanLogicalDevice.h"

namespace VoxelEngine 
{
	VulkanCommandContext::VulkanCommandContext(VulkanLogicalDevice* device)
		: m_device(device)
	{
		createCommandPool();
		createCommandBuffers();
	}
	void VulkanCommandContext::createCommandPool()
	{
		vk::CommandPoolCreateInfo poolInfo{};
		poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
		poolInfo.queueFamilyIndex = m_device->GetFamilyGraphicsIndex();

		commandPool = vk::raii::CommandPool(m_device->Get(), poolInfo);
	}

	void VulkanCommandContext::createCommandBuffers()
	{
		commandBuffers.clear();

		vk::CommandBufferAllocateInfo allocinfo{};
		allocinfo.commandPool = commandPool;
		allocinfo.level = vk::CommandBufferLevel::ePrimary;
		allocinfo.commandBufferCount = MAX_FRAMES_IN_FLIGHT;

		commandBuffers = vk::raii::CommandBuffers(m_device->Get(), allocinfo);
	}
}
