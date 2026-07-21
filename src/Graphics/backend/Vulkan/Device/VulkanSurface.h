#pragma once
#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

namespace Zero 
{
	class Window;

	class VulkanSurface
	{
	public:
		VulkanSurface(const vk::raii::Instance& instance, const Window& window);
		~VulkanSurface() = default;

		// Getter
		const vk::raii::SurfaceKHR& Get() const { return m_surface; }

	private:
		void createSurface(const vk::raii::Instance& instance, const Window& window);

	private:
		vk::raii::SurfaceKHR m_surface = nullptr;
	};
}
