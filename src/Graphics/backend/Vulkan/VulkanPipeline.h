#pragma once
#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

namespace VoxelEngine
{
	class VulkanLogicalDevice;
	class VulkanSwapchain;

	class VulkanPipeline
	{
	public:
		VulkanPipeline(VulkanLogicalDevice* device, VulkanSwapchain* swapchain);
		~VulkanPipeline() = default;

		// Getter
		const vk::raii::PipelineLayout& GetPipelineLayout() const { return pipelineLayout; };
		const vk::raii::Pipeline& GetGraphicsPipeline() const { return graphicsPipeline; };

	private:
		void createGraphicsPipeline();

	private:
		vk::raii::PipelineLayout pipelineLayout = nullptr;
		vk::raii::Pipeline graphicsPipeline = nullptr;

	private:
		// dependencies
		VulkanLogicalDevice* m_device;
		VulkanSwapchain* m_swapchain;
	};
}
