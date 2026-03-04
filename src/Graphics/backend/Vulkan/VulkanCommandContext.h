#pragma once
#pragma once
#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif
#include <vector>

namespace VoxelEngine
{
	const uint32_t MAX_FRAMES_IN_FLIGHT = 2;

	class VulkanLogicalDevice;

	class VulkanCommandContext
	{
	public:
		VulkanCommandContext(VulkanLogicalDevice* device);
		~VulkanCommandContext() = default;

		// Getter
		const vk::raii::CommandPool& GetCommandPool() const { return commandPool; };
		std::vector<vk::raii::CommandBuffer>& GetCommandBuffers() { return commandBuffers; };

	private:
		void createCommandPool();
		void createCommandBuffers();

	private:
		vk::raii::CommandPool commandPool = nullptr;
		std::vector<vk::raii::CommandBuffer> commandBuffers;

	private:
		VulkanLogicalDevice* m_device;
	};
}
