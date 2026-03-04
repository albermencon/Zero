#include "pch.h"
#include <Engine/Core.h>
#include "Graphics/backend/Vulkan/VulkanInstance.h"
#include "Graphics/backend/Vulkan/VulkanDebug.h"
#include <GLFW/glfw3.h>

namespace VoxelEngine 
{
	VulkanInstance::VulkanInstance()
	{
		createNewInstance();
		SetupDebugMessenger();
	}

	void VulkanInstance::createNewInstance()
	{
        vk::ApplicationInfo appInfo{};
        appInfo.pApplicationName = "Vulkan";
        //appInfo.apiVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "Voxel Engine";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = vk::ApiVersion13;

        vk::InstanceCreateInfo createInfo{};
        createInfo.pApplicationInfo = &appInfo;

        std::vector<const char*> extensions;

        // Get the required instance extension from GLFW
        uint32_t glfwExtensionCount = 0;
        auto glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

        // Add GLFW extensions
        for (uint32_t i = 0; i < glfwExtensionCount; ++i)
            extensions.push_back(glfwExtensions[i]);

#ifdef ENGINE_DEBUG
        // Add debug utils extension (REQUIRED for debug messenger)
        extensions.push_back(vk::EXTDebugUtilsExtensionName);
#endif

        // Validate extensions support
        auto extensionProperties = m_Context.enumerateInstanceExtensionProperties();

        for (const char* requiredExtension : extensions)
        {
            bool found = std::ranges::any_of(
                extensionProperties,
                [requiredExtension](const vk::ExtensionProperties& prop)
                {
                    return strcmp(prop.extensionName, requiredExtension) == 0;
                });

            if (!found)
            {
                ENGINE_CORE_ERROR("Required extension not supported: {}", requiredExtension);
                throw std::runtime_error(
                    std::string("Required extension not supported: ") + requiredExtension);
            }

        }

#ifdef ENGINE_DEBUG
        const std::vector<const char*> validationLayers =
        {
            "VK_LAYER_KHRONOS_validation"
        };

        auto supportedLayers = m_Context.enumerateInstanceLayerProperties();

        for (const char* layer : validationLayers)
        {
            bool found = std::ranges::any_of(
                supportedLayers,
                [layer](const vk::LayerProperties& prop)
                {
                    return strcmp(prop.layerName, layer) == 0;
                });

            if (!found)
            {
                ENGINE_CORE_ERROR("Validation layer not supported");
                throw std::runtime_error("Validation layer not supported");
            }
        }

        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();

        ENGINE_CORE_TRACE("Enabled VK_LAYER_KHRONOS_validation");
#endif

        createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        createInfo.ppEnabledExtensionNames = extensions.data();

        m_Instance = vk::raii::Instance(m_Context, createInfo);
	}

	void VulkanInstance::SetupDebugMessenger()
	{
#ifdef ENGINE_DEBUG
		vk::DebugUtilsMessageSeverityFlagsEXT severityFlags(vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose |
			vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
			vk::DebugUtilsMessageSeverityFlagBitsEXT::eError);
		vk::DebugUtilsMessageTypeFlagsEXT     messageTypeFlags(
			vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation);
		vk::DebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCreateInfoEXT{};
		debugUtilsMessengerCreateInfoEXT.messageSeverity = severityFlags;
		debugUtilsMessengerCreateInfoEXT.messageType = messageTypeFlags;
		debugUtilsMessengerCreateInfoEXT.pfnUserCallback = &Debug::debugCallback;

        m_DebugMessenger = vk::raii::DebugUtilsMessengerEXT(m_Instance, debugUtilsMessengerCreateInfoEXT);
#endif
	}

}
