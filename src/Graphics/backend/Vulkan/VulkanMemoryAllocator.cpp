#include "pch.h"
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include "Graphics/backend/Vulkan/VulkanMemoryAllocator.h"

namespace Zero 
{
    void VulkanMemory::Initialize(VkInstance instance,
        VkPhysicalDevice physicalDevice,
        VkDevice device)
    {
        VmaAllocatorCreateInfo allocatorInfo{};
        allocatorInfo.instance = instance;
        allocatorInfo.physicalDevice = physicalDevice;
        allocatorInfo.device = device;
        allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;

        VkResult result = vmaCreateAllocator(&allocatorInfo, &m_Allocator);
        if (result != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create VMA allocator");
        }
    }

	void VulkanMemory::Shutdown()
	{
		if (m_Allocator)
		{
			vmaDestroyAllocator(m_Allocator);
			m_Allocator = VK_NULL_HANDLE;
		}
	}
}
