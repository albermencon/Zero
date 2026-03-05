#pragma once
#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

namespace Zero 
{
	class VulkanPhysicalDevice
	{
	public:
		VulkanPhysicalDevice(const vk::raii::Instance& instance);
		~VulkanPhysicalDevice() = default;

		// Getter
		const vk::raii::PhysicalDevice& Get() const { return m_physicalDevice; }

	private:
		void pickPhysicalDevice(const vk::raii::Instance& instance);

	private:
		vk::raii::PhysicalDevice m_physicalDevice = nullptr;
	};
}
