#pragma once
#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

#include "Graphics/core/Pipeline.h"
#include <Engine/Graphics/PipelineDesc.h>

namespace Zero
{
	class VulkanLogicalDevice;
	class VulkanSwapchain;

	class VulkanPipeline : public Pipeline
	{
	public:
		VulkanPipeline(VulkanLogicalDevice* device, const PipelineDesc& desc);
		VulkanPipeline(VulkanLogicalDevice* device, VulkanSwapchain* swapchain);
		~VulkanPipeline() override = default;

		bool IsCompute() const noexcept override { return m_isCompute; }

		const vk::raii::PipelineLayout& GetPipelineLayout() const { return pipelineLayout; }
		const vk::raii::DescriptorSetLayout& GetDescriptorSetLayout() const { return descriptorSetLayout; }
		const vk::raii::Pipeline& GetGraphicsPipeline() const { return graphicsPipeline; }
		const vk::raii::Pipeline& GetComputePipeline() const { return computePipeline; }

	private:
		void createFromDesc(const PipelineDesc& desc);
		void createComputePipeline(const PipelineDesc& desc);
		void createLegacyGraphicsPipeline();

	private:
		bool m_isCompute = false;
		vk::raii::DescriptorSetLayout descriptorSetLayout = nullptr;
		vk::raii::PipelineLayout pipelineLayout = nullptr;
		vk::raii::Pipeline graphicsPipeline = nullptr;
		vk::raii::Pipeline computePipeline = nullptr;

		VulkanLogicalDevice* m_device;
		VulkanSwapchain* m_swapchain = nullptr;
	};
}
