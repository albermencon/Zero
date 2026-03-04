#include "pch.h"
#include "Graphics/backend/Vulkan/VulkanPhysicalDevice.h"
#include <Engine/Log.h>
#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

namespace VoxelEngine 
{
	VulkanPhysicalDevice::VulkanPhysicalDevice(const vk::raii::Instance& instance)
	{
		pickPhysicalDevice(instance);
	}
	void VulkanPhysicalDevice::pickPhysicalDevice(const vk::raii::Instance& instance)
	{
        std::vector<vk::raii::PhysicalDevice> devices = instance.enumeratePhysicalDevices();
        if (devices.empty())
        {
            ENGINE_CORE_ERROR("failed to find GPUs with Vulkan support");
            ENGINE_CORE_ERROR("failed to find GPUs with Vulkan support");
        }

        // Use an ordered map to automatically sort candidates by increasing score
        std::multimap<int, vk::raii::PhysicalDevice> candidates;

        for (const auto& device : devices) {
            auto deviceProperties = device.getProperties();
            auto deviceFeatures = device.getFeatures();
            uint32_t score = 0;

            if (deviceProperties.apiVersion < VK_API_VERSION_1_3)
            {
                continue;
            }

            // Discrete GPUs have a significant performance advantage
            if (deviceProperties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu) {
                score += 1000;
            }

            // Maximum possible size of textures affects graphics quality
            score += deviceProperties.limits.maxImageDimension2D;

            // Application can't function without geometry shaders
            if (!deviceFeatures.geometryShader) {
                continue;
            }
            candidates.insert(std::make_pair(score, device));
        }

        // Check if the best candidate is suitable at all
        if (candidates.rbegin()->first > 0) {
            m_physicalDevice = candidates.rbegin()->second;
        }
        else {
            ENGINE_CORE_ERROR("failed to find a suitable GPU");
            throw std::runtime_error("failed to find a suitable GPU!");
        }

        auto props = m_physicalDevice.getProperties();
        auto memProps = m_physicalDevice.getMemoryProperties();
        auto limits = props.limits;

        uint64_t deviceLocalBytes = 0;
        for (uint32_t i = 0; i < memProps.memoryHeapCount; i++)
        {
            if (memProps.memoryHeaps[i].flags & vk::MemoryHeapFlagBits::eDeviceLocal)
            {
                deviceLocalBytes += memProps.memoryHeaps[i].size;
            }
        }

        double vramGB = deviceLocalBytes / (1024.0 * 1024.0 * 1024.0);

        const char* vendorName = "Unknown";

        switch (props.vendorID)
        {
        case 0x1002: vendorName = "AMD"; break;
        case 0x10DE: vendorName = "NVIDIA"; break;
        case 0x8086: vendorName = "Intel"; break;
        case 0x13B5: vendorName = "ARM"; break;
        case 0x5143: vendorName = "Qualcomm"; break;
        }

        uint32_t apiVersion = props.apiVersion;
        uint32_t driverVersion = props.driverVersion;
        const char* deviceName = props.deviceName;
        ENGINE_CORE_INFO("GPU Name: {}", deviceName);
        ENGINE_CORE_INFO("Vulkan API Version: {}.{}.{}",
            VK_VERSION_MAJOR(apiVersion),
            VK_VERSION_MINOR(apiVersion),
            VK_VERSION_PATCH(apiVersion));
        ENGINE_CORE_INFO("Driver Version: {}.{}.{}",
            VK_VERSION_MAJOR(driverVersion),
            VK_VERSION_MINOR(driverVersion),
            VK_VERSION_PATCH(driverVersion));
        ENGINE_CORE_INFO("VRAM: {:.0f} GB", vramGB);
        ENGINE_CORE_INFO("Vendor: {0}", vendorName);
	}
}
