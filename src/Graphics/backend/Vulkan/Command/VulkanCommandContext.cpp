#include "pch.h"
#include "VulkanCommandContext.h"
#include "Graphics/backend/Vulkan/Device/VulkanLogicalDevice.h"
#include "Graphics/core/GraphicsCore.h"

namespace Zero 
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
		allocinfo.commandPool = *commandPool;
		allocinfo.level = vk::CommandBufferLevel::ePrimary;
		allocinfo.commandBufferCount = MAX_FRAMES_IN_FLIGHT;

		commandBuffers = vk::raii::CommandBuffers(m_device->Get(), allocinfo);
	}
}
