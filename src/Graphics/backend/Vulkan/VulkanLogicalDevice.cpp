#include "pch.h"
#include <Engine/Log.h>
#include "Graphics/backend/Vulkan/VulkanLogicalDevice.h"

namespace Zero 
{
	VulkanLogicalDevice::VulkanLogicalDevice(const vk::raii::SurfaceKHR& surface, const vk::raii::PhysicalDevice& physicalDevice)
	{
		createLogicalDevice(surface, physicalDevice);
	}
	void VulkanLogicalDevice::createLogicalDevice(const vk::raii::SurfaceKHR& surface, const vk::raii::PhysicalDevice& physicalDevice)
	{
        // find the index of the first queue family that supports graphics
        std::vector<vk::QueueFamilyProperties> queueFamilyProperties = physicalDevice.getQueueFamilyProperties();
        uint32_t graphicsIndex = findQueueFamilies(physicalDevice);

        // determine a queueFamilyIndex that supports present
        // first check if the graphicsIndex is good enough
        auto presentIndex = physicalDevice.getSurfaceSupportKHR(graphicsIndex, *surface)
            ? graphicsIndex
            : static_cast<uint32_t>(queueFamilyProperties.size());

        if (presentIndex == queueFamilyProperties.size())
        {
            // the graphicsIndex doesn't support present -> look for another family index that supports both
            // graphics and present
            for (size_t i = 0; i < queueFamilyProperties.size(); i++)
            {
                if ((queueFamilyProperties[i].queueFlags & vk::QueueFlagBits::eGraphics) &&
                    physicalDevice.getSurfaceSupportKHR(static_cast<uint32_t>(i), *surface))
                {
                    graphicsFamilyIndex = static_cast<uint32_t>(i);
                    presentIndex = graphicsIndex;
                    break;
                }
            }
            if (presentIndex == queueFamilyProperties.size())
            {
                // there's nothing like a single family index that supports both graphics and present -> look for another
                // family index that supports present
                for (size_t i = 0; i < queueFamilyProperties.size(); i++)
                {
                    if (physicalDevice.getSurfaceSupportKHR(static_cast<uint32_t>(i), *surface))
                    {
                        presentIndex = static_cast<uint32_t>(i);
                        break;
                    }
                }
            }
        }
        if ((graphicsIndex == queueFamilyProperties.size()) || (presentIndex == queueFamilyProperties.size()))
        {
            ENGINE_CORE_ERROR("Could not find a queue for graphics or present -> terminating");
            throw std::runtime_error("Could not find a queue for graphics or present -> terminating");
        }

        // Create a chain of feature structures
        vk::StructureChain<
            vk::PhysicalDeviceFeatures2,
            vk::PhysicalDeviceVulkan11Features,
            vk::PhysicalDeviceVulkan13Features,
            vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT
            //,vk::PhysicalDevicePresentModeFifoLatestReadyFeaturesKHR
        > featureChain{};

        // Enable features directly in the chain
        //featureChain.get<vk::PhysicalDevicePresentModeFifoLatestReadyFeaturesKHR>().presentModeFifoLatestReady = VK_TRUE;
        featureChain.get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering = VK_TRUE; // Enable dynamic rendering from Vulkan 1.3
        featureChain.get<vk::PhysicalDeviceVulkan13Features>().synchronization2 = VK_TRUE;
        featureChain.get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>().extendedDynamicState = VK_TRUE; // Enable extended dynamic state from the extension
        featureChain.get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters = VK_TRUE;
        std::vector<const char*> deviceExtensions = {
            vk::KHRSwapchainExtensionName
            //,vk::KHRPresentModeFifoLatestReadyExtensionName
        };

        std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
        std::set<uint32_t> uniqueQueueFamilies = { graphicsIndex, presentIndex };

        float queuePriority = 0.5f;
        for (uint32_t family : uniqueQueueFamilies)
        {
            vk::DeviceQueueCreateInfo deviceQueueCreateInfo{};
            deviceQueueCreateInfo.queueFamilyIndex = family;
            deviceQueueCreateInfo.queueCount = 1;
            deviceQueueCreateInfo.pQueuePriorities = &queuePriority;

            queueCreateInfos.push_back(deviceQueueCreateInfo);
        }

        vk::DeviceCreateInfo deviceCreateInfo{};
        deviceCreateInfo.pNext = &featureChain.get<vk::PhysicalDeviceFeatures2>();
        deviceCreateInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
        deviceCreateInfo.pQueueCreateInfos = queueCreateInfos.data();
        deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
        deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();

        // query device capabilities
        // Basic surface capabilities (min/max number of images in swap chain, min/max width and height of images)
        // Surface formats (pixel format, color space)
        // Available presentation modes
        auto surfaceCapabilities = physicalDevice.getSurfaceCapabilitiesKHR(*surface);

        m_device = vk::raii::Device(physicalDevice, deviceCreateInfo);
        m_graphicsQueue = vk::raii::Queue(m_device, graphicsIndex, 0);
        m_presentQueue = vk::raii::Queue(m_device, presentIndex, 0);

        graphicsFamilyIndex = graphicsIndex;
        presentFamilyIndex = presentIndex;
	}
    uint32_t VulkanLogicalDevice::findQueueFamilies(const vk::raii::PhysicalDevice& physicalDevice)
    {
        // find the index of the first queue family that supports graphics
        std::vector<vk::QueueFamilyProperties> queueFamilyProperties = physicalDevice.getQueueFamilyProperties();

        // get the first index into queueFamilyProperties which supports graphics
        auto graphicsQueueFamilyProperty =
            std::find_if(queueFamilyProperties.begin(),
                queueFamilyProperties.end(),
                [](vk::QueueFamilyProperties const& qfp) { return qfp.queueFlags & vk::QueueFlagBits::eGraphics; }
            );
        return static_cast<uint32_t>(std::distance(queueFamilyProperties.begin(), graphicsQueueFamilyProperty));
    }
}
