#pragma once
#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

namespace VoxelEngine 
{
	class VulkanLogicalDevice
	{
	public:
		VulkanLogicalDevice(const vk::raii::SurfaceKHR& surface, const vk::raii::PhysicalDevice& physicalDevice);
		~VulkanLogicalDevice() = default;

		// Getter
		const vk::raii::Device& Get() const { return m_device; }
		const vk::raii::Queue& GetGraphicsQueue() const { return m_graphicsQueue; }
		const vk::raii::Queue& GetPresentQueue() const { return m_presentQueue; }
		const uint32_t& GetFamilyGraphicsIndex() const { return graphicsFamilyIndex; }
		const uint32_t& GetFamilyPresentIndex() const { return presentFamilyIndex; }

	private:
		void createLogicalDevice(const vk::raii::SurfaceKHR& surface, const vk::raii::PhysicalDevice& physicalDevice);
		uint32_t findQueueFamilies(const vk::raii::PhysicalDevice& physicalDevice);

	private:
		vk::raii::Device m_device = nullptr;
		vk::raii::Queue m_graphicsQueue = nullptr;
		vk::raii::Queue m_presentQueue = nullptr;
		uint32_t graphicsFamilyIndex = UINT32_MAX;
		uint32_t presentFamilyIndex = UINT32_MAX;
	};
}
