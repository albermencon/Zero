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

	class VulkanComputePipeline : public ComputePipeline
	{
	public:
		VulkanComputePipeline(VulkanLogicalDevice* device, const ComputePipelineDesc& desc);
		~VulkanComputePipeline() override = default;

		[[nodiscard]] const vk::raii::PipelineLayout& GetPipelineLayout() const noexcept { return m_pipelineLayout; }
		[[nodiscard]] const vk::raii::DescriptorSetLayout& GetDescriptorSetLayout() const noexcept { return m_descriptorSetLayout; }
		[[nodiscard]] const vk::raii::Pipeline& GetPipeline() const noexcept { return m_pipeline; }
		[[nodiscard]] uint32_t GetPushConstantSize() const noexcept { return m_pushConstantSize; }

	private:
		void createComputePipeline(const ComputePipelineDesc& desc);

	private:
		vk::raii::DescriptorSetLayout m_descriptorSetLayout = nullptr;
		vk::raii::PipelineLayout m_pipelineLayout = nullptr;
		vk::raii::Pipeline m_pipeline = nullptr;
		uint32_t m_pushConstantSize = 0;

		VulkanLogicalDevice* m_device = nullptr;
	};
}

