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
	class VulkanLogicalDevice;
	class VulkanSwapchain;

	class VulkanSyncObjects
	{
	public:
		VulkanSyncObjects(VulkanLogicalDevice* device, VulkanSwapchain* swapchain);
		~VulkanSyncObjects() = default;

		// Getter
		const std::vector<vk::raii::Semaphore>& GetImageAvailableSemaphores() const { return m_presentCompletedSemaphores; };
		const std::vector<vk::raii::Semaphore>& GetRenderFinishedSemaphores() const { return m_renderFinishedSemaphores; };
		const std::vector<vk::raii::Fence>& GetInFlightFences() const { return m_inFlightFences; };
		const std::vector<vk::Fence>& GetImagesInFlight() const { return m_imagesInFlight; };

		void recreateSyncObjects();

	private:
		void createSyncObjetcs();

	private:
		std::vector<vk::raii::Semaphore> m_presentCompletedSemaphores;
		std::vector<vk::raii::Semaphore> m_renderFinishedSemaphores;
		std::vector<vk::raii::Fence> m_inFlightFences;
		std::vector<vk::Fence> m_imagesInFlight;
	private:
		// dependencies
		VulkanLogicalDevice* m_device;
		VulkanSwapchain* m_swapchain;
	};
}
