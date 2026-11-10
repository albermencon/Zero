#pragma once
#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

#include <string_view>
#include "Graphics/core/Pipeline.h"
#include <Engine/Graphics/PipelineDesc.h>

namespace Zero
{
	class VulkanLogicalDevice;

	class VulkanGraphicsPipeline : public GraphicsPipeline
	{
	public:
		VulkanGraphicsPipeline(VulkanLogicalDevice* device, const GraphicsPipelineDesc& desc);
		~VulkanGraphicsPipeline() override = default;

		[[nodiscard]] const vk::raii::PipelineLayout& GetPipelineLayout() const noexcept { return m_pipelineLayout; }
		[[nodiscard]] const vk::raii::DescriptorSetLayout& GetDescriptorSetLayout() const noexcept { return m_descriptorSetLayout; }
		[[nodiscard]] const vk::raii::Pipeline& GetPipeline() const noexcept { return m_pipeline; }
		[[nodiscard]] uint32_t GetPushConstantSize() const noexcept { return m_pushConstantSize; }
		[[nodiscard]] vk::ShaderStageFlags GetPushConstantStages() const noexcept { return m_pushConstantStages; }

	private:
		void createFromDesc(const GraphicsPipelineDesc& desc);

	private:
		vk::raii::DescriptorSetLayout m_descriptorSetLayout = nullptr;
		vk::raii::PipelineLayout m_pipelineLayout = nullptr;
		vk::raii::Pipeline m_pipeline = nullptr;
		uint32_t m_pushConstantSize = 0;
		vk::ShaderStageFlags m_pushConstantStages = {};

		VulkanLogicalDevice* m_device = nullptr;
	};
}
