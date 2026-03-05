#pragma once
#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

namespace Zero
{
	class Window;
	class VulkanLogicalDevice;
	class VulkanPhysicalDevice;
	class VulkanSurface;

	class VulkanSwapchain
	{
	public:
		VulkanSwapchain(VulkanLogicalDevice* Device, VulkanPhysicalDevice* PhysicalDevice,
			VulkanSurface* Surface, Window* window);
		~VulkanSwapchain() = default;

		void recreateSwapChain();

		// Getter
		const vk::raii::SwapchainKHR& Get() const { return m_Swapchain; };
		const std::vector<vk::Image>& GetImages() const { return m_Images; };
		const std::vector<vk::raii::ImageView>& GetImageViews() const { return m_ImageViews; };
		vk::PresentModeKHR GetPresentMode() const { return m_PresentMode; };
		vk::Format GetFormat() const { return m_ImageFormat; };
		vk::ColorSpaceKHR GetColorSpace() const { return m_ColorSpace; };
		vk::Extent2D GetExtent() const { return m_Extent; };

	private:
		void createSwapChain(vk::SwapchainKHR oldSwapchain = VK_NULL_HANDLE);

		void createImageViews();
		void cleanupSwapChain();
	private:
		vk::SurfaceFormatKHR chooseSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& avaibleFormats);
		vk::PresentModeKHR chooseSwapChainPresentMode(const std::vector<vk::PresentModeKHR>& availablePresentModes);
		vk::Extent2D chooseSwapExtent(const vk::SurfaceCapabilitiesKHR& capabilities);
	private:
		vk::raii::SwapchainKHR m_Swapchain = nullptr;
		std::vector<vk::Image> m_Images;
		std::vector<vk::raii::ImageView> m_ImageViews;
		vk::PresentModeKHR m_PresentMode;

		vk::Format m_ImageFormat;
		vk::ColorSpaceKHR m_ColorSpace;
		vk::Extent2D m_Extent;
	private:
		// dependencies
		VulkanLogicalDevice* m_Device;
		VulkanPhysicalDevice* m_PhysicalDevice;
		VulkanSurface* m_Surface;
		Window* m_window;
	};
}
