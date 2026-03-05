#pragma once
#include "Graphics/backend/Vulkan/VulkanInstance.h"
#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

namespace Zero 
{
	class VulkanInstance
	{
	public:
		VulkanInstance();
		~VulkanInstance() = default;

		// Getter
		const vk::raii::Instance& Get() const { return m_Instance; }
		
	private:
		void createNewInstance();
		void SetupDebugMessenger();

	private:
		vk::raii::Context m_Context;
		vk::raii::Instance m_Instance = nullptr;
#ifdef ENGINE_DEBUG
		vk::raii::DebugUtilsMessengerEXT m_DebugMessenger{ nullptr };
#endif
	};
}
